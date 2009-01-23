#include <sched.h>
#include <linux/unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "chip.h"
#include "sync_client.h"
#include "shared_mem.h"

// definitions
using namespace std;


THREADID chipThreadId()
{
   return  syscall( __NR_gettid );
}

//TODO: c++-ize this, please oh please!
CAPI_return_t chipInit(int rank)
{
   THREADID pin_tid = chipThreadId();


   pair<bool, UINT64> e = g_chip->core_map.find(pin_tid);

   //FIXME: Not sure what tid_map is, this should be enabled
   // if( g_chip->tid_map.find(rank)->first != false)
   // {
   //    ASSERT(false, "Error: Two cores tried to init the same rank!\n");
   // }

   if ( e.first == false ) {
      g_chip->tid_map[rank] = pin_tid;    
      g_chip->core_map.insert( pin_tid, rank );
   }   
   else
   {
      cerr << "chipInit Error initializing core twice: " << dec << rank << endl;
//      ASSERT(false, "Error: Core tried to init more than once!\n");
   }

   return 0;
}

CAPI_return_t chipInitFreeRank(int *rank)
{
   
	THREADID pin_tid = chipThreadId();

   GetLock (&g_chip->maps_lock, 1);

   pair<bool, UINT64> e = g_chip->core_map.find(pin_tid);

   //FIXME: Not sure what tid_map is, this should be enabled
   // if( g_chip->tid_map.find(rank)->first != false)
   // {
   //    ASSERT(false, "Error: Two cores tried to init the same rank!\n");
   // }

   if ( e.first == false ) {
      // Don't allow free initializion of the MCP which claimes the
      // highest core.
      for(int i = 0; i < g_chip->num_modules - 1; i++)
      {
          if (g_chip->tid_map[i] == UINT_MAX)
          {
              g_chip->tid_map[i] = pin_tid;    
              g_chip->core_map.insert( pin_tid, i );
              *rank = i;
				  // cerr << "chipInit initializing core: " << i << endl;
				  
				  ReleaseLock (&g_chip->maps_lock);
              return 0;
          }
      }

      // cerr << "chipInit Error: No Free Cores." << endl;
   }   
   else
   {
//      cerr << "chipInit Error initializing FREE core twice (pin_tid): " << (int)pin_tid << endl;
//      cerr << "chipInit: Keeping old rank: " << dec << (int)(g_chip->core_map.find(pin_tid).second) << endl;
//      ASSERT(false, "Error: Core tried to init more than once!\n");
   }

	ReleaseLock (&g_chip->maps_lock);

   return 0;
}

CAPI_return_t chipRank(int *rank)
{
   THREADID pin_tid = chipThreadId();

   pair<bool, UINT64> e = g_chip->core_map.find(pin_tid);
   *rank = (e.first == false) ? -1 : e.second;

   bool rank_ok = (*rank < g_chip->getNumModules());
   ASSERT(rank_ok, "Illegal rank value returned by chipRank!\n");

   return 0;
}

CAPI_return_t commRank(int *commid)
{
   int my_tid;
   chipRank(&my_tid);
   // FIXME: The network nodes should be moved up from within a core to
   //  directly within a chip.  When this happens, this will have to change.
   
   *commid = (my_tid < 0) ? -1 : g_chip->core[my_tid].getNetwork()->getTransport()->ptCommID();

   return 0;
}

CAPI_return_t chipSendW(CAPI_endpoint_t sender, CAPI_endpoint_t receiver, 
                        char *buffer, int size)
{
   int rank;
   chipRank(&rank);
   return g_chip->core[rank].coreSendW(sender, receiver, buffer, size);
}

CAPI_return_t chipRecvW(CAPI_endpoint_t sender, CAPI_endpoint_t receiver, 
                        char *buffer, int size)
{
   int rank;
   chipRank(&rank);
   /*
   cout << "Starting receive on core " << rank << " (from:" << sender 
	<< ", to:" << receiver << ", ptr:" << hex << &buffer << dec
	<< ", size:" << size << ")" << endl;
   */ 
   return g_chip->core[rank].coreRecvW(sender, receiver, buffer, size);
}

// performance model wrappers

void perfModelRun(int rank, PerfModelIntervalStat *interval_stats, bool firstCallInIntrvl)
{ 
   //int rank; 
   //chipRank(&rank);
   assert(0 <= rank && rank < g_chip->num_modules);
   g_chip->core[rank].perfModelRun(interval_stats, firstCallInIntrvl); 
}

void perfModelRun(int rank, PerfModelIntervalStat *interval_stats, 
                  REG *reads, UINT32 num_reads, bool firstCallInIntrvl)
{ 
   //int rank;
   //chipRank(&rank);
   assert(0 <= rank && rank < g_chip->num_modules);
   g_chip->core[rank].perfModelRun(interval_stats, reads, num_reads, firstCallInIntrvl); 
}

void perfModelRun(int rank, PerfModelIntervalStat *interval_stats, bool dcache_load_hit, 
                  REG *writes, UINT32 num_writes, bool firstCallInIntrvl)
{ 
   //int rank;
   //chipRank(&rank);
   assert(0 <= rank && rank < g_chip->num_modules);
   g_chip->core[rank].perfModelRun(interval_stats, dcache_load_hit, writes, num_writes, 
                                   firstCallInIntrvl); 
}


PerfModelIntervalStat** perfModelAnalyzeInterval(const string& parent_routine, 
                                                 const INS& start_ins, const INS& end_ins)
{ 
   // using zero is a dirty hack 
   // assumes its safe to use core zero to generate perfmodels for all cores
   assert(g_chip->num_modules > 0);
   
   //FIXME: These stats should be deleted at the end of execution
   PerfModelIntervalStat* *array = new PerfModelIntervalStat*[g_chip->num_modules];

   for (INT32 i = 0; i < g_chip->num_modules; i++)
      array[i] = g_chip->core[0].perfModelAnalyzeInterval(parent_routine, start_ins, end_ins);

   return array; 
}

void perfModelLogICacheLoadAccess(int rank, PerfModelIntervalStat *stats, bool hit)
{ 
   //int rank;
   //chipRank(&rank);
   assert(0 <= rank && rank < g_chip->num_modules);
   g_chip->core[rank].perfModelLogICacheLoadAccess(stats, hit); 
}
     
void perfModelLogDCacheStoreAccess(int rank, PerfModelIntervalStat *stats, bool hit)
{ 
    //int rank;
    //chipRank(&rank);
   assert(0 <= rank && rank < g_chip->num_modules);
   g_chip->core[rank].perfModelLogDCacheStoreAccess(stats, hit); 
}

void perfModelLogBranchPrediction(int rank, PerfModelIntervalStat *stats, bool correct)
{ 
   //int rank;
   //chipRank(&rank);
   assert(0 <= rank && rank < g_chip->num_modules);
   g_chip->core[rank].perfModelLogBranchPrediction(stats, correct); 
}


// organic cache model wrappers

bool icacheRunLoadModel(IntPtr i_addr, UINT32 size)
{ 
   int rank;
   chipRank(&rank);
   assert(0 <= rank && rank < g_chip->num_modules);
   return g_chip->core[rank].icacheRunLoadModel(i_addr, size); 
}

bool dcacheRunModel(CacheBase::AccessType access_type, IntPtr d_addr, char* data_buffer, UINT32 data_size)
{
   int rank;
   chipRank(&rank);
   assert(0 <= rank && rank < g_chip->num_modules);
	//TODO make everything use the cachebase::accesstype enum

	if( access_type == CacheBase::k_ACCESS_TYPE_LOAD)
		return g_chip->core[rank].dcacheRunModel(Core::LOAD, d_addr, data_buffer, data_size); 
	else
		return g_chip->core[rank].dcacheRunModel(Core::STORE, d_addr, data_buffer, data_size); 
}

/*
bool dcacheRunLoadModel(IntPtr d_addr, UINT32 size)
{ 
   //int rank;
   //chipRank(&rank);
   assert(0 <= rank && rank < g_chip->num_modules);
//   return g_chip->core[rank].dcacheRunLoadModel(d_addr, size); 
	char data_buffer[size];
   //TODO load data from cache into data_buffer. should buffer be on the stack?
	return g_chip->core[rank].dcacheRunModel(Core::LOAD, d_addr, data_buffer, size); 
}

bool dcacheRunStoreModel(int rank, IntPtr d_addr, UINT32 size)
{ 
   //int rank;
   //chipRank(&rank);
   assert(0 <= rank && rank < g_chip->num_modules);
//   return g_chip->core[rank].dcacheRunStoreModel(d_addr, size); 
	//TODO just passing in dummy data for now
	char data_buffer[size];
	for(unsigned int i = 0; i < size; i++)
		data_buffer[i] = (char) i;
   return g_chip->core[rank].dcacheRunModel(Core::STORE, d_addr, data_buffer, size); 
}
*/
// syscall model wrappers
void syscallEnterRunModel(CONTEXT *ctx, SYSCALL_STANDARD syscall_standard)
{
   int rank;
   chipRank(&rank);

   if(rank >= 0)
      g_chip->core[rank].getSyscallMdl()->runEnter(ctx, syscall_standard);
}

void syscallExitRunModel(CONTEXT *ctx, SYSCALL_STANDARD syscall_standard)
{
   int rank;
   chipRank(&rank);

   if(rank >= 0)
      g_chip->core[rank].getSyscallMdl()->runExit(ctx, syscall_standard);
}

// Sync client wrappers

void SimMutexInit(carbon_mutex_t *mux)
{
   int rank;
   chipRank(&rank);

   if(rank >= 0)
     g_chip->core[rank].getSyncClient()->mutexInit(mux);
}

void SimMutexLock(carbon_mutex_t *mux)
{
   int rank;
   chipRank(&rank);

   if(rank >= 0)
     g_chip->core[rank].getSyncClient()->mutexLock(mux);
}

void SimMutexUnlock(carbon_mutex_t *mux)
{
   int rank;
   chipRank(&rank);

   if(rank >= 0)
      g_chip->core[rank].getSyncClient()->mutexUnlock(mux);
}

void SimCondInit(carbon_cond_t *cond)
{
   int rank;
   chipRank(&rank);

   if(rank >= 0)
      g_chip->core[rank].getSyncClient()->condInit(cond);
}

void SimCondWait(carbon_cond_t *cond, carbon_mutex_t *mux)
{
   int rank;
   chipRank(&rank);

   if(rank >= 0)
      g_chip->core[rank].getSyncClient()->condWait(cond, mux);
}

void SimCondSignal(carbon_cond_t *cond)
{
   int rank;
   chipRank(&rank);

   if(rank >= 0)
      g_chip->core[rank].getSyncClient()->condSignal(cond);
}

void SimCondBroadcast(carbon_cond_t *cond)
{
   int rank;
   chipRank(&rank);

   if(rank >= 0)
      g_chip->core[rank].getSyncClient()->condBroadcast(cond);
}

void SimBarrierInit(carbon_barrier_t *barrier, UINT32 count)
{
   int rank;
   chipRank(&rank);

   if(rank >= 0)
      g_chip->core[rank].getSyncClient()->barrierInit(barrier, count);
}

void SimBarrierWait(carbon_barrier_t *barrier)
{
   int rank;
   chipRank(&rank);

   if(rank >= 0)
      g_chip->core[rank].getSyncClient()->barrierWait(barrier);
}

// == Chip class =============================== //

Chip::Chip(int num_mods): num_modules(num_mods), core_map(3*num_mods), shmem_tid_to_core_map(3*num_mods), prev_rank(0) 
//Chip::Chip(int num_mods): num_modules(num_mods), core_map(3*num_mods), prev_rank(0)
{
	InitLock (&maps_lock);
	
   tid_map = new THREADID [num_mods];
   core_to_shmem_tid_map = new THREADID [num_mods];

   core = new Core[num_mods];

   // Need to subtract 1 for the MCP
	for(int i = 0; i < num_mods; i++) 
   {
      tid_map[i] = UINT_MAX;
      core_to_shmem_tid_map[i] = UINT_MAX;
      core[i].coreInit(i, num_mods);
   }

	// FIXME: A hack
	aliasEnable = false;
	
	// Hack for chipFInishHack
	finished_cores = new bool[num_modules];
	for (int i = 0; i < num_modules; i++) {
		finished_cores[i] = false;
	}

	cerr << "Finished Chip Constructor." << endl;
}

void Chip::fini(int code, void *v)
{
   ofstream out( g_knob_output_file.Value().c_str() );

   for(int i = 0; i < num_modules; i++)
   {
      cout << "*** Core[" << i << "] summary ***" << endl;
      out << "*** Core[" << i << "] summary ***" << endl;
      core[i].fini(code, v, out); 
      cout << endl;
      out << endl;
   }

   out.close();
}

int Chip::registerSharedMemThread()
{
	// FIXME: I need to lock because there is a race condition
	THREADID pin_tid = chipThreadId();

   GetLock (&maps_lock, 1);
   pair<bool, UINT64> e = g_chip->shmem_tid_to_core_map.find(pin_tid);

   // If this thread isn't registered
   if ( e.first == false ) {

      // Search for an unused core to map this shmem thread to
      // one less to account for the MCP
      for(int i = 0; i < g_chip->num_modules; i++)
      {
          // Unused slots are set to UINT_MAX
          // FIXME: Use a different constant than UINT_MAX
          if(g_chip->core_to_shmem_tid_map[i] == UINT_MAX)
          {
              g_chip->core_to_shmem_tid_map[i] = pin_tid;    
              g_chip->shmem_tid_to_core_map.insert( pin_tid, i );
				  ReleaseLock (&maps_lock);
              return i;
          }
      }

      cerr << "chipInit Error: No Free Cores." << endl;
   }   
   else
   {
       cerr << "Initialized shared mem thread twice -- id: " << pin_tid << endl;
		 ReleaseLock (&maps_lock);
		 // FIXME: I think this is OK
       return g_chip->shmem_tid_to_core_map.find(pin_tid).second;
   }

	ReleaseLock (&maps_lock);
   return -1;
}

void Chip::debugSetInitialMemConditions (vector<IntPtr>& address_vector, 
		  											  vector< pair<INT32, DramDirectoryEntry::dstate_t> >& dram_vector, vector<vector<UINT32> >& sharers_list_vector, 
													  vector< vector< pair<INT32, CacheState::cstate_t> > >& cache_vector, 
		  											  vector<char*>& d_data_vector, 
													  vector<char*>& c_data_vector)
{
	vector<IntPtr> temp_address_vector = address_vector;

	assert (d_data_vector.size() == c_data_vector.size());
	assert (d_data_vector.size() == dram_vector.size());
	assert (cache_vector.size() == dram_vector.size() );

	while (!dram_vector.empty())
	{  //TODO does this assume 1:1 core/dram allocation?

		IntPtr curr_address = address_vector.back();
		address_vector.pop_back();

		INT32 curr_dram_id = dram_vector.back().first;
		DramDirectoryEntry::dstate_t curr_dstate = dram_vector.back().second;
      dram_vector.pop_back();

		vector<UINT32> curr_sharers_list = sharers_list_vector.back();
		sharers_list_vector.pop_back();

		char *curr_d_data = d_data_vector.back();
		d_data_vector.pop_back();

		core[curr_dram_id].debugSetDramState(curr_address, curr_dstate, curr_sharers_list, curr_d_data);
   }

	address_vector = temp_address_vector;

	while(!cache_vector.empty()) 
	{

		IntPtr curr_address = address_vector.back();
		address_vector.pop_back();
		
		vector< pair<INT32, CacheState::cstate_t> > curr_cache_vector = cache_vector.back();
		cache_vector.pop_back();

		char *curr_c_data = c_data_vector.back();
		c_data_vector.pop_back();

		while (!curr_cache_vector.empty()) {
			 
			INT32 curr_cache_id = curr_cache_vector.back().first;
			CacheState::cstate_t curr_cstate = curr_cache_vector.back().second;
      	curr_cache_vector.pop_back();

			core[curr_cache_id].debugSetCacheState(curr_address, curr_cstate, curr_c_data);
		}
   }

}

bool Chip::debugAssertMemConditions (vector<IntPtr>& address_vector, 
		  										 vector< pair<INT32, DramDirectoryEntry::dstate_t> >& dram_vector, vector<vector<UINT32> >& sharers_list_vector, 
												 vector< vector< pair<INT32, CacheState::cstate_t> > >& cache_vector, 
		  										 vector<char*>& d_data_vector, 
												 vector<char*>& c_data_vector,
												 string test_code, string error_string)
{
	bool all_asserts_passed = true;
	vector<IntPtr> temp_address_vector = address_vector;

	assert (d_data_vector.size() == c_data_vector.size());
	assert (d_data_vector.size() == dram_vector.size());
	assert (cache_vector.size() == dram_vector.size() );

	while (!dram_vector.empty())
	{  //TODO does this assume 1:1 core/dram allocation?

		IntPtr curr_address = address_vector.back();
		address_vector.pop_back();

		INT32 curr_dram_id = dram_vector.back().first;
		DramDirectoryEntry::dstate_t curr_dstate = dram_vector.back().second;
      dram_vector.pop_back();

		vector<UINT32> curr_sharers_list = sharers_list_vector.back();
		sharers_list_vector.pop_back();

		char *curr_d_data = d_data_vector.back();
		d_data_vector.pop_back();

		if (! core[curr_dram_id].debugAssertDramState(curr_address, curr_dstate, curr_sharers_list, curr_d_data))
			 all_asserts_passed = false;
   }

	address_vector = temp_address_vector;

	while(!cache_vector.empty()) 
	{

		IntPtr curr_address = address_vector.back();
		address_vector.pop_back();
		
		vector< pair<INT32, CacheState::cstate_t> > curr_cache_vector = cache_vector.back();
		cache_vector.pop_back();

		char *curr_c_data = c_data_vector.back();
		c_data_vector.pop_back();

		while (!curr_cache_vector.empty()) {
			 
			INT32 curr_cache_id = curr_cache_vector.back().first;
			CacheState::cstate_t curr_cstate = curr_cache_vector.back().second;
      	curr_cache_vector.pop_back();

			if (! core[curr_cache_id].debugAssertCacheState(curr_address, curr_cstate, curr_c_data))
				 all_asserts_passed = false;
		}
   }

	if(!all_asserts_passed) 
	{
   	cerr << "    *** ASSERTION FAILED *** : " << error_string << endl;
	}

	return all_asserts_passed;
}

/*user program calls get routed through this */
CAPI_return_t chipDebugSetMemState(IntPtr address, INT32 dram_address_home_id, DramDirectoryEntry::dstate_t dstate, CacheState::cstate_t cstate0, CacheState::cstate_t cstate1, vector<UINT32> sharers_list, char *d_data, char *c_data)
{
	vector<IntPtr> address_vector;
	vector< pair<INT32, DramDirectoryEntry::dstate_t> > dram_vector;
	vector< vector <UINT32> > sharers_list_vector;
	vector< vector < pair<INT32, CacheState::cstate_t> > > cache_vector;
	vector< pair<INT32, CacheState::cstate_t> > curr_cache_vector;

	vector<char*> d_data_vector;
	vector<char*> c_data_vector;

	assert (g_chip->aliasMap.find(address) != g_chip->aliasMap.end());
	address = g_chip->aliasMap[address];
	address_vector.push_back(address);

	dram_vector.push_back( pair<INT32, DramDirectoryEntry::dstate_t>(dram_address_home_id, dstate) );
	sharers_list_vector.push_back(sharers_list);

	curr_cache_vector.push_back( pair<INT32, CacheState::cstate_t>(0, cstate0) );
	curr_cache_vector.push_back( pair<INT32, CacheState::cstate_t>(1, cstate1) );
	cache_vector.push_back(curr_cache_vector);

	d_data_vector.push_back(d_data);
	c_data_vector.push_back(c_data);

	// cerr << "ChipDebug Set: d_data = 0x" << hex << (UINT32) d_data << ", c_data = 0x" << hex << (UINT32) c_data << endl; 
	
	g_chip->debugSetInitialMemConditions (address_vector, 
		  											  dram_vector, sharers_list_vector, 
													  cache_vector, 
		  											  d_data_vector, 
													  c_data_vector);

	return 0;
}

CAPI_return_t chipDebugAssertMemState(IntPtr address, INT32 dram_address_home_id, DramDirectoryEntry::dstate_t dstate, CacheState::cstate_t cstate0, CacheState::cstate_t cstate1, vector<UINT32> sharers_list, char *d_data, char *c_data, string test_code, string error_code)
{
	vector<IntPtr> address_vector;
	vector< pair<INT32, DramDirectoryEntry::dstate_t> > dram_vector;
	vector< vector <UINT32> > sharers_list_vector;
	vector< vector < pair<INT32, CacheState::cstate_t> > > cache_vector;
	vector< pair<INT32, CacheState::cstate_t> > curr_cache_vector;

	vector<char*> d_data_vector;
	vector<char*> c_data_vector;

	assert (g_chip->aliasMap.find(address) != g_chip->aliasMap.end());
	
	address = g_chip->aliasMap[address];
	address_vector.push_back(address);

	dram_vector.push_back( pair<INT32, DramDirectoryEntry::dstate_t>(dram_address_home_id, dstate) );
	sharers_list_vector.push_back(sharers_list);

	curr_cache_vector.push_back( pair<INT32, CacheState::cstate_t>(0, cstate0) );
	curr_cache_vector.push_back( pair<INT32, CacheState::cstate_t>(1, cstate1) );
	cache_vector.push_back(curr_cache_vector);

	d_data_vector.push_back(d_data);
	c_data_vector.push_back(c_data);
	
	// cerr << "ChipDebug Assert: d_data = 0x" << hex << (UINT32) d_data << ", c_data = 0x" << hex << (UINT32) c_data << endl; 
	
	if (g_chip->debugAssertMemConditions (address_vector, 
		  											  dram_vector, sharers_list_vector, 
													  cache_vector, 
		  											  d_data_vector, 
													  c_data_vector,
													  test_code, error_code) )
	{
		return 1;
	}
	else
	{
		return 0;
	}

	/*
	//	cout << " [Chip] Asserting Mem State " << endl;
	vector< pair<INT32, DramDirectoryEntry::dstate_t> > dram_vector;
	vector< pair<INT32, CacheState::cstate_t> > cache_vector;
			
	dram_vector.push_back( pair<INT32, DramDirectoryEntry::dstate_t>(dram_address_home_id, dstate) );
					
	cache_vector.push_back( pair<INT32, CacheState::cstate_t>(1, cstate1) );
	cache_vector.push_back( pair<INT32, CacheState::cstate_t>(0, cstate0) );

	//TODO return true/false based on whether assert is true or not?
	if(g_chip->debugAssertMemConditions(address, dram_vector, cache_vector, sharers_list, test_code, error_code))
	{
		return 1;
	} else {
		return 0;
	}
	*/

}


// FIXME: Stupid Hack for debugging purpose 
CAPI_return_t chipAlias (IntPtr address, addr_t addrType, UINT32 num)
{
	// It is better to create an alias map here. An assciative array
	assert (g_chip->num_modules == 3);
	switch (addrType) {

		case(DRAM_0):
			g_chip->aliasMap[address] = createAddress(num,0,false,false);
			break;
		case(DRAM_1):
			g_chip->aliasMap[address] = createAddress(num,1,false,false);
			break;
		/*
		case(DRAM_00):
			g_chip->aliasMap[address] = createAddress(num,0,false,true);
			break;
		case(DRAM_01):
			g_chip->aliasMap[address] = createAddress(num,0,true,true);
			break;
		case(DRAM_10):
			g_chip->aliasMap[address] = createAddress(num,1,true,true);
			break;
		case(DRAM_11):
			g_chip->aliasMap[address] = createAddress(num,1,false,true);
			break;
		*/
		default:
			cerr << "ERROR: chip.cc: Should not reach here\n";
			break;
	}
	   
		   
	g_chip->aliasEnable = true;

	cerr << "Aliasing address 0x" << hex << address << "  ==>  0x"  << hex << g_chip->aliasMap[address] << endl;  
	return 0;
}

extern KNOB<UINT32> g_knob_ahl_param;

IntPtr createAddress (UINT32 num, UINT32 coreId, bool pack1, bool pack2) {

	/*
	 * ADDRESS breaks down as follows
	 * ****************************************
	 * Assume logCacheBlockSize = 5
	 * Assume logBlockSize = 10
	 * Assume sizeof(IntPtr) = 32
	 * ****************************************
	 *  31             11 |    10    | 9               5 | 4                0 |
	 * |                  |          |                   |                    | 
	 * |       num        |  coreId  |  DRAMBlockOffset  |  cacheBlockOffset  |
	 * |                  |          |                   |                    |
	 */
	
	/*
	UINT32 logCacheBlockSize = log(g_knob_line_size);
	UINT32 cacheBlockOffset = ( (pack2) ? (g_knob_line_size - 2) : 0 );
	UINT32 DRAMBlockOffset = ( (pack1) ? ( (1 << (g_knob_ahl_param - logCacheBlockSize)) - 1) : 0 );

	return ( (num << (g_knob_ahl_param + 1) )  |  (coreId << g_knob_ahl_param)  |  (DRAMBlockOffset << logCacheBlockSize)  |  (cacheBlockOffset)  );
	*/
	
	// UINT32 num_modules = g_chip->num_modules;
	return ( ( (num * 3) + coreId) << g_knob_ahl_param);

}

UINT32 log(UINT32 value) {
	
	UINT32 k = 0; 
	while (!(value & 0x1)) {
		k++;
		value = value >> 1;
	}
	assert (k >= 0);

	return (k);
}

bool isAliasEnabled () {
 	return (g_chip->aliasEnable);
}

