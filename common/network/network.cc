#include <string.h>

#include "transport.h"
#include "core.h"
#include "network.h"
#include "log.h"

using namespace std;

// FIXME: Rework netCreateBuf and netExPacket. We don't need to
// duplicate the sender/receiver info the packet. This should be known
// by the transport layer and given to us. We also should be more
// intelligent about the time stamps, right now the method is very
// ugly.

Network::Network(Core *core)
      : _core(core)
{
   LOG_ASSERT_ERROR(sizeof(g_type_to_static_network_map) / sizeof(EStaticNetwork) == NUM_PACKET_TYPES,
                    "Static network type map has incorrect number of entries.");

   _numMod = Config::getSingleton()->getTotalCores();
   _tid = _core->getId();

   _transport = Transport::getSingleton()->createNode(_core->getId());

   _callbacks = new NetworkCallback [NUM_PACKET_TYPES];
   _callbackObjs = new void* [NUM_PACKET_TYPES];
   for (SInt32 i = 0; i < NUM_PACKET_TYPES; i++)
      _callbacks[i] = NULL;

   UInt32 modelTypes[NUM_STATIC_NETWORKS];
   Config::getSingleton()->getNetworkModels(modelTypes);

   for (SInt32 i = 0; i < NUM_STATIC_NETWORKS; i++)
      _models[i] = NetworkModel::createModel(this, modelTypes[i], (EStaticNetwork)i);

   LOG_PRINT("Initialized.");
}

Network::~Network()
{
   for (SInt32 i = 0; i < NUM_STATIC_NETWORKS; i++)
      delete _models[i];

   delete [] _callbackObjs;
   delete [] _callbacks;

   delete _transport;

   LOG_PRINT("Destroyed.");
}

void Network::registerCallback(PacketType type, NetworkCallback callback, void *obj)
{
   assert((UInt32)type < NUM_PACKET_TYPES);
   _callbacks[type] = callback;
   _callbackObjs[type] = obj;
}

void Network::unregisterCallback(PacketType type)
{
   assert((UInt32)type < NUM_PACKET_TYPES);
   _callbacks[type] = NULL;
}

void Network::outputSummary(std::ostream &out) const
{
   out << "Network summary:\n";
   for (UInt32 i = 0; i < NUM_STATIC_NETWORKS; i++)
   {
      out << "  Network model " << i << ":\n";
      _models[i]->outputSummary(out);
   }
}

// Polling function that performs background activities, such as
// pulling from the physical transport layer and routing packets to
// the appropriate queues.

void Network::netPullFromTransport()
{
   do
   {
      LOG_PRINT("Entering netPullFromTransport");

      NetPacket packet(_transport->recv());
 
      LOG_PRINT("Pull packet : type %i, from %i, time %llu", (SInt32)packet.type, packet.sender, packet.time);
      assert(0 <= packet.sender && packet.sender < _numMod);
      LOG_ASSERT_ERROR(0 <= packet.type && packet.type < NUM_PACKET_TYPES, "Packet type: %d not between 0 and %d", packet.type, NUM_PACKET_TYPES);

      // was this packet sent to us, or should it just be forwarded?
      if (packet.receiver != _core->getId())
      {
         // Disable this feature now. None of the network models use it
         // LOG_PRINT("Forwarding packet : type %i, from %i, time %llu.", (SInt32)packet.type, packet.sender, packet.time);
         // forwardPacket(packet);

         // if this isn't a broadcast message, then we shouldn't process it further
         if (packet.receiver != NetPacket::BROADCAST)
            continue;
      }

      // asynchronous I/O support
      NetworkCallback callback = _callbacks[packet.type];

      if (callback != NULL)
      {
         LOG_PRINT("Executing callback on packet : type %i, from %i, time %llu.", (SInt32)packet.type, packet.sender, packet.time);
         assert(0 <= packet.sender && packet.sender < _numMod);
         assert(0 <= packet.type && packet.type < NUM_PACKET_TYPES);

         callback(_callbackObjs[packet.type], packet);

         delete [] (Byte*) packet.data;
      }

      // synchronous I/O support
      else
      {
         LOG_PRINT("Enqueuing packet : type %i, from %i, time %llu.", (SInt32)packet.type, packet.sender, packet.time);
         _netQueueLock.acquire();
         _netQueue.push_back(packet);
         _netQueueLock.release();
         _netQueueCond.broadcast();
      }
   }
   while (_transport->query());
}

// FIXME: Can forwardPacket be subsumed by netSend?

void Network::forwardPacket(const NetPacket &packet)
{
   LOG_PRINT_ERROR("Forwarding of packets is currently disabled");

   NetworkModel *model = _models[g_type_to_static_network_map[packet.type]];

   vector<NetworkModel::Hop> hopVec;
   model->routePacket(packet, hopVec);

   Byte *buffer = packet.makeBuffer();
   UInt64 *timeStamp = (UInt64*)buffer;

   for (UInt32 i = 0; i < hopVec.size(); i++)
   {
      *timeStamp = hopVec[i].time;
      _transport->send(hopVec[i].dest, buffer, packet.bufferSize());
   }

   delete [] buffer;
}

SInt32 Network::netSend(NetPacket packet)
{
   assert(packet.type >= 0 && packet.type < NUM_PACKET_TYPES);
   assert(packet.sender == _core->getId());

   NetworkModel *model = _models[g_type_to_static_network_map[packet.type]];

   vector<NetworkModel::Hop> hopVec;
   model->routePacket(packet, hopVec);

   Byte *buffer = packet.makeBuffer();
   UInt64 *timeStamp = (UInt64*)buffer;

   for (UInt32 i = 0; i < hopVec.size(); i++)
   {
      LOG_PRINT("Send packet : type %i, to %i, time %llu", (SInt32) packet.type, hopVec[i].dest, hopVec[i].time);
      LOG_ASSERT_ERROR(hopVec[i].time >= packet.time, "hopVec[%d].time(%llu) < packet.time(%llu)", i, hopVec[i].time, packet.time);
      *timeStamp = hopVec[i].time;
      _transport->send(hopVec[i].dest, buffer, packet.bufferSize());
   }

   delete [] buffer;

   LOG_PRINT("Sent packet");

   return packet.length;
}

// Stupid helper class to eliminate special cases for empty
// sender/type vectors in a NetMatch
class NetRecvIterator
{
   public:
      NetRecvIterator(UInt32 i)
            : _mode(INT)
            , _max(i)
            , _i(0)
      {
      }
      NetRecvIterator(const std::vector<SInt32> &v)
            : _mode(SENDER_VECTOR)
            , _senders(&v)
            , _i(0)
      {
      }
      NetRecvIterator(const std::vector<PacketType> &v)
            : _mode(TYPE_VECTOR)
            , _types(&v)
            , _i(0)
      {
      }

      inline UInt32 get()
      {
         switch (_mode)
         {
         case INT:
            return _i;
         case SENDER_VECTOR:
            return (UInt32)_senders->at(_i);
         case TYPE_VECTOR:
            return (UInt32)_types->at(_i);
         default:
            assert(false);
            return (UInt32)-1;
         };
      }

      inline Boolean done()
      {
         switch (_mode)
         {
         case INT:
            return _i >= _max;
         case SENDER_VECTOR:
            return _i >= _senders->size();
         case TYPE_VECTOR:
            return _i >= _types->size();
         default:
            assert(false);
            return true;
         };
      }

      inline void next()
      {
         ++_i;
      }

      inline void reset()
      {
         _i = 0;
      }

   private:
      enum
      {
         INT, SENDER_VECTOR, TYPE_VECTOR
      } _mode;

      union
      {
         UInt32 _max;
         const std::vector<SInt32> *_senders;
         const std::vector<PacketType> *_types;
      };

      UInt32 _i;
};

NetPacket Network::netRecv(const NetMatch &match)
{
   LOG_PRINT("Entering netRecv.");

   // Track via iterator to minimize copying
   NetQueue::iterator itr;
   Boolean found;

   found = false;

   NetRecvIterator sender = match.senders.empty()
                            ? NetRecvIterator(_numMod)
                            : NetRecvIterator(match.senders);

   NetRecvIterator type = match.types.empty()
                          ? NetRecvIterator((UInt32)NUM_PACKET_TYPES)
                          : NetRecvIterator(match.types);

   LOG_ASSERT_ERROR(_core && _core->getPerformanceModel(),
                    "Core and/or performance model not initialized.");
   UInt64 start_time = _core->getPerformanceModel()->getCycleCount();

   _netQueueLock.acquire();

   while (!found)
   {
      itr = _netQueue.end();

      // check every entry in the queue
      for (NetQueue::iterator i = _netQueue.begin();
            i != _netQueue.end();
            i++)
      {
         // only find packets that match
         for (sender.reset(); !sender.done(); sender.next())
         {
            if (i->sender != (SInt32)sender.get())
               continue;

            for (type.reset(); !type.done(); type.next())
            {
               if (i->type != (PacketType)type.get())
                  continue;

               found = true;

               // find the earliest packet
               if (itr == _netQueue.end() ||
                   itr->time > i->time)
               {
                  itr = i;
               }
            }
         }
      }

      // go to sleep until a packet arrives if none have been found
      if (!found)
      {
         _netQueueCond.wait(_netQueueLock);
      }
   }

   assert(found == true && itr != _netQueue.end());
   assert(0 <= itr->sender && itr->sender < _numMod);
   assert(0 <= itr->type && itr->type < NUM_PACKET_TYPES);
   assert(itr->receiver == _core->getId());

   // Copy result
   NetPacket packet = *itr;
   _netQueue.erase(itr);
   _netQueueLock.release();

   if (packet.time > start_time)
   {
      Instruction *i = new RecvInstruction(packet.time - start_time);
      _core->getPerformanceModel()->queueDynamicInstruction(i);
   }

   LOG_PRINT("Exiting netRecv : type %i, from %i", (SInt32)packet.type, packet.sender);

   return packet;
}

// -- Wrappers

SInt32 Network::netSend(SInt32 dest, PacketType type, const void *buf, UInt32 len)
{
   NetPacket packet;
   assert(_core && _core->getPerformanceModel());
   packet.time = _core->getPerformanceModel()->getCycleCount();
   packet.sender = _core->getId();
   packet.receiver = dest;
   packet.length = len;
   packet.type = type;
   packet.data = buf;

   return netSend(packet);
}

SInt32 Network::netBroadcast(PacketType type, const void *buf, UInt32 len)
{
   return netSend(NetPacket::BROADCAST, type, buf, len);
}

NetPacket Network::netRecv(SInt32 src, PacketType type)
{
   NetMatch match;
   match.senders.push_back(src);
   match.types.push_back(type);
   return netRecv(match);
}

NetPacket Network::netRecvFrom(SInt32 src)
{
   NetMatch match;
   match.senders.push_back(src);
   return netRecv(match);
}

NetPacket Network::netRecvType(PacketType type)
{
   NetMatch match;
   match.types.push_back(type);
   return netRecv(match);
}

void Network::enableModels()
{
   for (int i = 0; i < NUM_STATIC_NETWORKS; i++)
   {
      _models[i]->enable();
   }
}

void Network::disableModels()
{
   for (int i = 0; i < NUM_STATIC_NETWORKS; i++)
   {
      _models[i]->disable();
   }
}

// -- NetPacket

NetPacket::NetPacket()
   : time(0)
   , type(INVALID)
   , sender(INVALID_CORE_ID)
   , receiver(INVALID_CORE_ID)
   , length(0)
   , data(0)
{
}

NetPacket::NetPacket(UInt64 t, PacketType ty, SInt32 s,
                     SInt32 r, UInt32 l, const void *d)
   : time(t)
   , type(ty)
   , sender(s)
   , receiver(r)
   , length(l)
   , data(d)
{
}


NetPacket::NetPacket(Byte *buffer)
{
   NetPacket *buff_pkt = (NetPacket*)buffer;

   *this = *buff_pkt;

   Byte *data_buffer = new Byte[length];
   memcpy(data_buffer, &buff_pkt->data, length);
   data = data_buffer;

   delete [] buffer;
}

// This implementation is slightly wasteful because there is no need
// to copy the const void* value in the NetPacket when length == 0,
// but I don't see this as a major issue.
UInt32 NetPacket::bufferSize() const
{
   return (length < sizeof(const void*)) 
      ? sizeof(NetPacket) 
      : (sizeof(NetPacket) - sizeof(const void*) + length);
}

Byte* NetPacket::makeBuffer() const
{
   UInt32 size = bufferSize();
   assert(size >= sizeof(NetPacket));

   Byte *buffer = new Byte[size];

   NetPacket *buff_pkt = (NetPacket*)buffer;

   *buff_pkt = *this;

   memcpy(&buff_pkt->data, this->data, length);

   return buffer;
}
