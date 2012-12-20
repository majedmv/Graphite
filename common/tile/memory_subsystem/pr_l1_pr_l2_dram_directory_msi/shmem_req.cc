#include "shmem_req.h"
#include "clock_converter.h"
#include "log.h"

namespace PrL1PrL2DramDirectoryMSI
{

ShmemReq::ShmemReq(ShmemMsg* shmem_msg, UInt64 time)
   : _time(time)
{
   // Make a local copy of the shmem_msg
   _shmem_msg = new ShmemMsg(shmem_msg);
   LOG_ASSERT_ERROR(!shmem_msg->getDataBuf(), "Shmem Reqs should not have data payloads");
}

ShmemReq::~ShmemReq()
{
   delete _shmem_msg;
}

void
ShmemReq::updateTime(UInt64 time)
{
   if (_time < time)
      _time = time;
}

void
ShmemReq::updateInternalVariablesOnFrequencyChange(float old_frequency, float new_frequency)
{
   _time = convertCycleCount(_time, old_frequency, new_frequency);
}

}
