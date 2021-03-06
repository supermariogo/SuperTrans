/**
 * @file
 * @author  jpoe   <>, (C) 2008, 2009
 * @date    09/19/08
 * @brief   This is the implementation for the transaction reporting module.
 *
 * @section LICENSE
 * Copyright: See COPYING file that comes with this distribution
 *
 * @section DESCRIPTION
 * C++ Implementation: transReport
 */
/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////

#include "transReport.h"


/**
 * @ingroup transReport
 * @brief   Constructor
 * 
 * @param reportFileName 
 */
transReport::transReport(const char *reportFileName)
{
    time_t rawtime;
    struct tm * timeinfo;
    char filename[120],buffer[40];
    int i,j;

    char *name = strdup(SescConf->getCharPtr("TransactionalMemory","traceFile"));

    if(strcmp(name,""))
      sprintf(filename,"%s-%s",reportFileName,name);
    else
      sprintf(filename,"%s",reportFileName);

    time ( &rawtime );

    timeinfo = localtime ( &rawtime );

    strftime(buffer,40,"-%b%d.%Y-%H.%M.%S-tmDebug",timeinfo);
    strcat(filename,buffer);

    if(SescConf->getInt("TransactionalMemory","traceToFile"))
      outfile = fopen(filename,"a");
    else
      outfile = stderr;

    if(SescConf->getInt("TransactionalMemory","printRealBCTimes"))
      printRealBCTimes = 1;
    else 
      printRealBCTimes = 0;

    if(SescConf->getInt("TransactionalMemory","printDetailedTrace"))
      printDetailedTrace = 1;
    else
      printDetailedTrace = 0;

    if(SescConf->getInt("TransactionalMemory","printSummaryReport"))
      printSummaryReport = 1;
    else
      printSummaryReport = 0;

    transactionalReport = 0;
    calculateFullReadWriteSet = 0;
    printTransactionalReportSummary = 0;
    printTransactionalReportDetail = 0;

    if( SescConf->getInt("TransactionalMemory","printTransactionalReport") )
    {
      transactionalReport = 1;
      printTransactionalReportDetail = 1;
      fprintf(outfile,"<Trans> tmReport:HEADER:UTID:PID:CPU:TID:PC:ABORT?:INST_COUNT:READ_SET_SIZE:READS:WRITE_SET_SIZE:WRITES:CYCLE_LENGTH:READ_SET:WRITE_SET:CONFLICT_LIST:FP_OP_COUNT:COMMITTED_INSTCOUNT_AT_BEGIN:STALLED_CYCLES\n");
    }

    if (SescConf->getInt("TransactionalMemory","printTransactionalReportSummary") )
    {
      transactionalReport = 1;
      printTransactionalReportSummary = 1;
    }

    if(SescConf->getInt("TransactionalMemory","calculateFullReadWriteSet"))
    {
      transactionalReport = 1;
      calculateFullReadWriteSet = 1;
    }

    if(SescConf->getInt("TransactionalMemory","printAllNacks"))
      printAllNacks = 1;
    else 
      printAllNacks = 0;

    if(SescConf->getInt("TransactionalMemory","recordTransMemRefs"))
      recordTransMemRefs = 1;
    else 
      recordTransMemRefs = 0;

    maxCount = SescConf->getInt("TransactionalMemory","transReportFlush");
    outCount = maxCount;

    summaryCommitCount = 0;
    summaryCommitInstCount = 0;
    summaryCommitCycleCount = 0;
    summaryAbortCount = 0;
    summaryAbortInstCount = 0;

    summaryUsefulNackCount = 0;
    summaryUsefulNackCycle = 0;
    summaryAbortedNackCount = 0;
    summaryAbortedNackCycle = 0;

    summaryMinCommitInstCount = 999999999;
    summaryMaxCommitInstCount = 0;
    summaryMinAbortInstCount = 999999999;
    summaryMaxAbortInstCount = 0;
    summaryMinCommitCycleCount = 999999999;
    summaryMaxCommitCycleCount = 0;
    summaryMinAbortCycleCount = 999999999;
    summaryMaxAbortCycleCount = 0;
    summaryReadSetSize = 0;
    summaryWriteSetSize = 0;
    summaryLoadCount = 0;
    summaryStoreCount = 0;

    for(i = 0; i < MAX_CPU_COUNT; i++)
    {
      nackingAddr[i] = 0;
      nackingTimestamp[i] = 0;
      nackingPid[i] = -1;
      tmDepth[i] = 0;

      summaryBeginCycle[i]=0;
      summaryNackCycle[i]=0;
      summaryTransFlag[i]=0;

      for(j = 0; j < 6; j++)
        tempInstCount[i][j]=0;
      for(j = 0; j < 6; j++)
        tempInstCountAbort[i][j]=0;

      transMemRefState[i] = 0;

      committedInstCountByCpu[i] = 0;
    }


  if(SescConf->getInt("TransactionalMemory","enableBeginTMStats"))
    recordTMStart = 1;
  else
    recordTMStart = 0;

  beginRecordkeepingInstructionCount=0;
  beginRecordKeepingCycleCount=0;

   emptyList.clear();
}

/**
 * @ingroup transReport
 * @brief   report commit
 * 
 * @param pid  Process ID
 */
void transReport::reportCommit(int pid)
{
  struct transRef temp = commits[pid].front();
  commits[pid].pop();

  tmDepth[pid]--;
  //! Right now we are subsuming nest transactions, so we need to check if this is a nested begin
  if(tmDepth[pid] > 0)
  {
    if(printDetailedTrace)
      fprintf(outfile,"<Trans> tmTrace: CMSB :%lld:%d:9999:%d:%llu:%llu\n"
                                          ,temp.utid
                                          ,temp.pid
                                          ,temp.tid
                                          ,temp.timestamp
                                          ,globalClock);
  }
  else
  {
    if(printDetailedTrace)
      fprintf(outfile,"<Trans> tmTrace: CM   :%lld:%d:1005:%d:%d:%d:%d:%d:%d:%d:%llu:%llu\n"
                                          ,temp.utid,temp.pid,temp.tid
                                          ,tempInstCount[pid][transLoad]
                                          ,tempInstCount[pid][transStore]
                                          ,tempInstCount[pid][transInt]
                                          ,tempInstCount[pid][transFp]
                                          ,tempInstCount[pid][transBJ]
                                          ,tempInstCount[pid][transFence]
                                          ,temp.timestamp
                                          ,globalClock);

   INSTCOUNT instCount = tempInstCount[pid][transLoad] + tempInstCount[pid][transStore];
   instCount += tempInstCount[pid][transInt] + tempInstCount[pid][transFp] + tempInstCount[pid][transBJ] + tempInstCount[pid][transFence];

   if(printSummaryReport)
      summaryCommit(temp.pid,instCount,globalClock);

   if(transactionalReport)
      transactionalCommit(temp.utid,instCount,globalClock,tempInstCount[pid][transFp]);

    if(recordTransMemRefs)
      transMemRef_newCommit(temp.pid);

  }


  fflush(stdout); //! Always fflush on a Commit
}

/**
 * @note
 * CPU IS SAVED FOR THE ENTIRE TRANSACTION, THUS THIS ASSUMES
 * NO MIGRATION OF IN-FLIGHT TRANSACTIONS.  OR, TRANSACTION WILL BE
 * PRINTED IN STATISTICS AS PART OF THE CPU IT BEGAN ON.
*/

/**
 * @ingroup transReport
 * @brief   report begin
 * 
 * @param pid  Process ID
 * @param cpu  CPU ID
 */
void transReport::reportBegin(int pid, int cpu)
{
  struct transRef temp = begins[pid].front();
  begins[pid].pop();

  tmDepth[pid]++;
  //! Right now we are subsuming nest transactions, so we need to check if this is a nested begin
  if(tmDepth[pid] > 1)
  {
    if(printDetailedTrace)
      fprintf(outfile,"<Trans> tmTrace: BGSB :%lld:%d:9999:%d:%0#10x:%llu:%llu\n"
                                            ,temp.utid
                                            ,temp.pid
                                            ,temp.tid
                                            ,temp.PC
                                            ,temp.timestamp
                                            ,globalClock);
  }
  else
  {
    //! Reset the transactional Instruction Counter
    tempInstCount[pid][0] = 0;
    tempInstCount[pid][1] = 0;
    tempInstCount[pid][2] = 0;
    tempInstCount[pid][3] = 0;
    tempInstCount[pid][4] = 0;
    tempInstCount[pid][5] = 0;

    if(printDetailedTrace)
      fprintf(outfile,"<Trans> tmTrace: BG   :%lld:%d:1000:%d:%0#10x:%llu:%llu\n"
                                            ,temp.utid
                                            ,temp.pid
                                            ,temp.tid
                                            ,temp.PC
                                            ,temp.timestamp
                                            ,globalClock);

    if(printSummaryReport)
      summaryBegin(temp.pid,globalClock);

    if(transactionalReport)
      transactionalBegin(temp.utid,temp.tid,temp.pid,temp.PC,cpu,globalClock);

    if(recordTransMemRefs)
      transMemRef_newBegin(temp.pid,temp.PC);
  }

  registerOut();
}

/**
 * @ingroup transReport
 * @brief   increment transactional instruction count
 * 
 * @param pid  Process ID
 * @param type Transactional instruction type
 */
void transReport::registerTransInst(int pid, transInstType type)
{
  tempInstCount[pid][type]++;
}

/**
 * @ingroup transReport
 * @brief   increment transactional instruciton count (for aborts)
 * 
 * @param pid  Process ID
 * @param type Transactional instruction type
 *
 * @note
 * This count is very similiar to the previous, but happens at the fetch stage so that we can get
 * instruction counts of the Aborted transactions
 */
void transReport::registerTransInstAbort(int pid, transInstType type)
{
  if(type < 6)
    tempInstCountAbort[pid][type]++;
}

/**
 * @ingroup transReport
 * @brief   register begin (at fetch)
 * 
 * @param utid    Unique Tx ID
 * @param pid  Process ID
 * @param tid  ThreadID
 * @param PC   Program counter
 * @param begin_timestamp Cycle when Tx begins
 */
void transReport::registerBegin(ID utid, int pid, int tid, RAddr PC, TIMESTAMP begin_timestamp)
{
  struct transRef temp;
  temp.pid = pid;
  temp.tid = tid;
  temp.PC = PC;
  temp.utid = utid;
  temp.timestamp = begin_timestamp;


  if(printRealBCTimes)
  {
      fprintf(outfile,"<Trans> tmTrace: BG!! :%lld:%d:9998:%d:%0#10x:%llu:%llu\n"
                                          ,temp.utid
                                          ,temp.pid
                                          ,temp.tid
                                          ,temp.PC
                                          ,temp.timestamp
                                          ,globalClock);

    registerOut();
  }
  if(tmDepth[pid] <= 1)
  {
    //! Reset the transactional Abort Instruction Counter
    for(int j = 0; j < 6; j++)
      tempInstCountAbort[pid][j] = 0;
  }
  begins[pid].push(temp);
  nackingAddr[pid] = 0;
  nackingTimestamp[pid] = 0;
  nackingPid[pid] = -1;  
}

/**
 * @ingroup transReport
 * @brief   register abort (at fetch)
 * 
 * @param utid    Unique Tx ID
 * @param pid  Process ID
 * @param tid  ThreadID
 * @param begin_timestamp Cycle when Tx begins
 */
void transReport::registerCommit(ID utid,int pid, int tid, TIMESTAMP begin_timestamp)
{
  struct transRef temp;
  temp.pid = pid;
  temp.tid = tid;
  temp.utid = utid;
  temp.timestamp = begin_timestamp;

  if(printRealBCTimes)
  {
      fprintf(outfile,"<Trans> tmTrace: CM!! :%lld:%d:9999:%d:%llu:%llu\n"
                                            ,temp.utid
                                            ,temp.pid
                                            ,temp.tid
                                            ,temp.timestamp
                                            ,globalClock);

    registerOut();
  }
  commits[pid].push(temp);
  nackingAddr[pid] = 0;
  nackingTimestamp[pid] = 0;
  nackingPid[pid] = -1;
}

/**
 * @ingroup transReport
 * @brief   report nack resolved
 * 
 * @param utid    Unique Tx ID
 * @param pid  Process ID
 * @param tid  ThreadID
 * @param begin_timestamp Cycle when Tx begins
 *
 * @note
 * This can not be embedded into the commit function as it is in memory
 * functions for lazy approaches since that would also include the commit
 * time in the nack delay.
 */
void transReport::reportNackCommitFN(ID utid, int pid, int tid, unsigned long long begin_timestamp)
{
  //! If we are able to commit in a Lazy approach and we were stalling, print NKFN
  if(nackingPid[pid] != -1)
  {
    if(printDetailedTrace)
      fprintf(outfile,"<Trans> tmTrace: NKFN :%lld:%d:1008:%d:%d:%llu:%llu:%llu\n"
                                            ,utid,pid
                                            ,tid
                                            ,nackingPid[pid]
                                            ,nackingTimestamp[pid]
                                            ,begin_timestamp
                                            ,globalClock);
    registerOut();

    if(printSummaryReport)
      summaryNackFinish(pid,globalClock);

    if(transactionalReport)
      transactionalNackFinish(utid, globalClock);
  }

}

/**
 * @ingroup transReport
 * @brief   report load (retire)
 * 
 * @param pid  Process ID
 */
void transReport::reportLoad(int pid){
  struct memRef temp = loads[pid].front();
  loads[pid].pop();
  tempInstCount[pid][transLoad]++;
  if(printDetailedTrace)
    fprintf(outfile,"<Trans> tmTrace: LD   :%lld:%d:1001:%d:%#10x:%#10x:%llu:%llu\n"
                                            ,temp.utid
                                            ,pid
                                            ,temp.tid
                                            ,temp.raddr
                                            ,temp.caddr
                                            ,temp.timestamp
                                            ,globalClock);

  if(printSummaryReport)
    summaryLoad(pid,temp.caddr);

  if(transactionalReport)
      transactionalLoad(temp.utid,temp.caddr);

  if(recordTransMemRefs)
    transMemRef_newLoad(temp.beginPC, temp.raddr);

  registerOut();
}

/**
 * @ingroup transReport
 * @brief   report store (retire)
 * 
 * @param pid  Process ID
 */
void transReport::reportStore(int pid){
  struct memRef temp = stores[pid].front();
  stores[pid].pop();
  tempInstCount[pid][transStore]++;
  if(printDetailedTrace)
    fprintf(outfile,"<Trans> tmTrace: ST   :%lld:%d:1002:%d:%#10x:%#10x:%llu:%llu\n"
                                            ,temp.utid
                                            ,pid
                                            ,temp.tid
                                            ,temp.raddr
                                            ,temp.caddr
                                            ,temp.timestamp
                                            ,globalClock);

  if(printSummaryReport)
    summaryStore(pid,temp.caddr);

   if(transactionalReport)
      transactionalStore(temp.utid, temp.caddr);

  if(recordTransMemRefs)
    transMemRef_newStore(temp.beginPC, temp.raddr);


  registerOut();
}

/**
 * @ingroup transReport
 * @brief   register load (fetch)
 * 
 * @param utid    Unique Tx ID
 * @param beginPC PC when Tx begins
 * @param pid  Process ID
 * @param tid  ThreadID
 * @param raddr Real address
 * @param caddr Cache address
 * @param begin_timestamp Cycle when Tx begins
 */
void transReport::registerLoad(ID utid,RAddr beginPC, int pid, int tid, RAddr raddr,RAddr caddr, TIMESTAMP begin_timestamp)
{
  if(nackingAddr[pid] != 0)
  {
    if(printDetailedTrace)
      fprintf(outfile,"<Trans> tmTrace: NKFN :%lld:%d:1006:%d:%d:%#10x:%llu:%llu:%llu\n"
                                            ,utid
                                            ,pid
                                            ,tid
                                            ,nackingPid[pid]
                                            ,nackingAddr[pid]
                                            ,nackingTimestamp[pid]
                                            ,begin_timestamp
                                            ,globalClock);

    if(printSummaryReport)
      summaryNackFinish(pid,globalClock);

    if(transactionalReport)
      transactionalNackFinish(utid,globalClock);

    registerOut();
  }

  struct memRef temp;
  temp.caddr = caddr;
  temp.raddr = raddr;
  temp.utid = utid;
  temp.pid = pid;
  temp.tid = tid;
  temp.beginPC = beginPC;
  temp.timestamp = begin_timestamp;
  loads[pid].push(temp);
  nackingAddr[pid] = 0;
  nackingTimestamp[pid] = 0;
  nackingPid[pid] = -1;
}

/**
 * @ingroup transReport
 * @brief   register store (fetch)
 * 
 * @param utid    Unique Tx ID
 * @param beginPC PC when Tx begins
 * @param pid  Process ID
 * @param tid  ThreadID
 * @param raddr Real address
 * @param caddr Cache address
 * @param begin_timestamp Cycle when Tx begins
 */
void transReport::registerStore(ID utid,RAddr beginPC,int pid, int tid, RAddr raddr,RAddr caddr,TIMESTAMP begin_timestamp)
{
  if(nackingAddr[pid] != 0)
  {
    if(printDetailedTrace)
      fprintf(outfile,"<Trans> tmTrace: NKFN :%lld:%d:1006:%d:%d:%#10x:%llu:%llu:%llu\n"
                                                ,utid
                                                ,pid
                                                ,tid
                                                ,nackingPid[pid]
                                                ,nackingAddr[pid]
                                                ,nackingTimestamp[pid]
                                                ,begin_timestamp
                                                ,globalClock);

    if(printSummaryReport)
      summaryNackFinish(pid,globalClock);

    if(transactionalReport)
      transactionalNackFinish(utid, globalClock);

    registerOut();
  }


  struct memRef temp;
  temp.caddr = caddr;
  temp.raddr = raddr;
  temp.pid = pid;
  temp.utid = utid;
  temp.tid = tid;
  temp.beginPC = beginPC;
  temp.timestamp = begin_timestamp;
  stores[pid].push(temp);
  nackingAddr[pid] = 0;
  nackingTimestamp[pid] = 0;
  nackingPid[pid] = -1;
}

/**
 * @ingroup transReport
 * @brief   report nack on load
 * 
 * @param utid    Unique Tx ID
 * @param pid  Process ID
 * @param tid  ThreadID
 * @param nackPid Process ID of nacking thread
 * @param raddr Real address
 * @param caddr cache address
 * @param myTimestamp my timestamp
 * @param nackTimestamp timestamp of nacker
 */
void transReport::reportNackLoad(ID utid,int pid, int tid, int nackPid, RAddr raddr, RAddr caddr, TIMESTAMP myTimestamp, TIMESTAMP nackTimestamp)
{
  //!  This will only print out the NACK if it is a new, unique NACK.  Note the timestamp must not be equal to ((~0ULL) - 1024) because this
  //!  1indicates that a transaction is in the process of ABORTING, but has yet to complete its ABORT.
    if(printAllNacks || ((nackingAddr[pid] != caddr || nackingTimestamp[pid] != nackTimestamp || nackingPid[pid] != nackPid) && nackTimestamp != ((~0ULL) - 1024)))
    {
      if(nackingAddr[pid] != 0)
      {
        if(printDetailedTrace)
          fprintf(outfile,"<Trans> tmTrace: NKFN :%lld:%d:1006:%d:%d:%#10x:%llu:%llu:%llu\n"
                                                  ,utid
                                                  ,pid
                                                  ,tid
                                                  ,nackingPid[pid]
                                                  ,nackingAddr[pid]
                                                  ,nackingTimestamp[pid]
                                                  ,myTimestamp
                                                  ,globalClock);

        if(printSummaryReport)
          summaryNackFinish(pid,globalClock);

        if(transactionalReport)
            transactionalNackFinish(utid, globalClock);

        registerOut();
      }

      if(printDetailedTrace)
        fprintf(outfile,"<Trans> tmTrace: NKLD :%lld:%d:1003:%d:%d:%#10x:%#10x:%llu:%llu:%llu\n"
                                                  ,utid
                                                  ,pid
                                                  ,tid
                                                  ,nackPid
                                                  ,raddr
                                                  ,caddr
                                                  ,nackTimestamp
                                                  ,myTimestamp
                                                  ,globalClock);

      if(printSummaryReport)
        summaryNackBegin(pid,globalClock);

      if(transactionalReport)
         transactionalNackBegin(utid, tid, nackPid, caddr, globalClock);

      nackingAddr[pid] = caddr;
      nackingTimestamp[pid] = nackTimestamp;
      nackingPid[pid] = nackPid;
      registerOut();
    }
}

/**
 * @ingroup transReport
 * @brief   report nack on store
 * 
 * @param utid    Unique Tx ID
 * @param pid  Process ID
 * @param tid  ThreadID
 * @param nackPid Process ID of nacking thread
 * @param raddr Real address
 * @param caddr cache adress
 * @param myTimestamp my timestamp
 * @param nackTimestamp timestamp of nacker
 */
void transReport::reportNackStore(ID utid,int pid, int tid, int nackPid, RAddr raddr, RAddr caddr, TIMESTAMP myTimestamp, TIMESTAMP nackTimestamp)
{
  //!  This will only print out the NACK if it is a new, unique NACK.  Note the timestamp must not be equal to ((~0ULL) - 1024) because this
  //!  indicates that a transaction is in the process of ABORTING, but has yet to complete its ABORT.  You can disable this check with printAllNacks.
  if(printAllNacks || ((nackingAddr[pid] != caddr || nackingTimestamp[pid] != nackTimestamp || nackingPid[pid] != nackPid) && nackTimestamp != ((~0ULL) - 1024)))
  {
    if(nackingAddr[pid] != 0)
    {
      if(printDetailedTrace)
        fprintf(outfile,"<Trans> tmTrace: NKFN :%lld:%d:1006:%d:%d:%#10x:%llu:%llu:%llu\n"
                                                    ,utid
                                                    ,pid
                                                    ,tid
                                                    ,nackingPid[pid]
                                                    ,nackingAddr[pid]
                                                    ,nackingTimestamp[pid]
                                                    ,myTimestamp
                                                    ,globalClock);

      if(printSummaryReport)
        summaryNackFinish(pid,globalClock);

      if(transactionalReport)
         transactionalNackFinish(utid, globalClock);

      registerOut();
    }
      if(printDetailedTrace)
        fprintf(outfile,"<Trans> tmTrace: NKST :%lld:%d:1003:%d:%d:%#10x:%#10x:%llu:%llu:%llu\n"
                                                    ,utid
                                                    ,pid
                                                    ,tid
                                                    ,nackPid
                                                    ,raddr
                                                    ,caddr
                                                    ,nackTimestamp
                                                    ,myTimestamp
                                                    ,globalClock);

      if(printSummaryReport)
        summaryNackBegin(pid,globalClock);

      if(transactionalReport)
         transactionalNackBegin(utid, tid, nackPid, caddr, globalClock);

      nackingAddr[pid] = caddr;
      nackingTimestamp[pid] = nackTimestamp;
      nackingPid[pid] = nackPid;
      registerOut();
  }
}

/**
 * @ingroup transReport
 * @brief   report nack on commit
 * 
 * @param utid    Unique Tx ID
 * @param pid  Process ID
 * @param tid  ThreadID
 * @param nackPid Process ID of nacking thread
 * @param myTimestamp my timestamp
 * @param nackTimestamp timestamp of nacker
 */
void transReport::reportNackCommit(ID utid,int pid, int tid, int nackPid, TIMESTAMP myTimestamp, TIMESTAMP nackTimestamp)
{
  if(printAllNacks || ((nackingTimestamp[pid] != nackTimestamp || nackingPid[pid] != nackPid) && nackTimestamp != ((~0ULL) - 1024)))
  {
    if(nackingPid[pid] != -1)
    {
      if(printDetailedTrace)
        fprintf(outfile,"<Trans> tmTrace: NKFN :%lld:%d:1008:%d:%d:%llu:%llu:%llu\n"
                                                    ,utid
                                                    ,pid
                                                    ,tid
                                                    ,nackingPid[pid]
                                                    ,nackingTimestamp[pid]
                                                    ,myTimestamp
                                                    ,globalClock);

      if(printSummaryReport)
        summaryNackFinish(pid,globalClock);

      if(transactionalReport)
         transactionalNackFinish(utid, globalClock);

      registerOut();
    }
      if(printDetailedTrace)
        fprintf(outfile,"<Trans> tmTrace: NKCM :%lld:%d:1007:%d:%d:%llu:%llu:%llu\n"
                                                    ,utid
                                                    ,pid
                                                    ,tid
                                                    ,nackPid
                                                    ,nackTimestamp
                                                    ,myTimestamp
                                                    ,globalClock);

      if(printSummaryReport)
        summaryNackBegin(pid,globalClock);

      if(transactionalReport)
         transactionalNackBegin(utid, tid, nackPid, 0, globalClock);

      nackingTimestamp[pid] = nackTimestamp;
      nackingPid[pid] = nackPid;
      registerOut();
  }
}

/**
 * @ingroup transReport
 * @brief   report abort
 * 
 * @param utid    Unique Tx ID
 * @param pid  Process ID
 * @param tid  ThreadID
 * @param nackPid Process ID of nacking thread
 * @param raddr Real address
 * @param caddr Cache address
 * @param myTimestamp my timestamp
 * @param nackTimestamp timestamp of aborter
 */
void transReport::reportAbort(ID utid,int pid, int tid, int nackPid, RAddr raddr, RAddr caddr, TIMESTAMP myTimestamp, TIMESTAMP nackTimestamp)
{
  //! Handle the case for the Eager approaches
  if(nackingAddr[pid] != 0)
  {
    if(printDetailedTrace)
      fprintf(outfile,"<Trans> tmTrace: NKFN :%lld:%d:1006:%d:%d:%#10x:%llu:%llu:%llu\n"
                                                    ,utid
                                                    ,pid
                                                    ,tid
                                                    ,nackingPid[pid]
                                                    ,nackingAddr[pid]
                                                    ,nackingTimestamp[pid]
                                                    ,myTimestamp
                                                    ,globalClock);

    if(printSummaryReport)
      summaryNackFinish(pid,globalClock);

    if(transactionalReport)
      transactionalNackFinish(utid, globalClock);

    registerOut();
  }  
  //! Handle the case for Lazy approaches
  else if(nackingPid[pid] != -1)
  {
    if(printDetailedTrace)
      fprintf(outfile,"<Trans> tmTrace: NKFN :%lld:%d:1008:%d:%d:%llu:%llu:%llu\n"
                                                    ,utid
                                                    ,pid
                                                    ,tid
                                                    ,nackingPid[pid]
                                                    ,nackingTimestamp[pid]
                                                    ,myTimestamp
                                                    ,globalClock);

    if(printSummaryReport)
      summaryNackFinish(pid,globalClock);

    if(transactionalReport)
      transactionalNackFinish(utid, globalClock);

    registerOut();
  }
  struct memRef temp;
  struct transRef transTemp;
  tmDepth[pid]--;
  if(printDetailedTrace || printRealBCTimes)
    fprintf(outfile,"<Trans> tmTrace: AB   :%lld:%d:1004:%d:%d:%#10x:%#10x:%d:%d:%d:%d:%d:%d:%llu:%llu:%llu\n"
                                                    ,utid
                                                    ,pid
                                                    ,tid
                                                    ,nackPid
                                                    ,raddr
                                                    ,caddr
                                                    ,tempInstCountAbort[pid][transLoad]
                                                    ,tempInstCountAbort[pid][transStore]
                                                    ,tempInstCountAbort[pid][transInt]
                                                    ,tempInstCountAbort[pid][transFp]
                                                    ,tempInstCountAbort[pid][transBJ]
                                                    ,tempInstCountAbort[pid][transFence]
                                                    ,nackTimestamp
                                                    ,myTimestamp
                                                    ,globalClock);

  INSTCOUNT instCount = tempInstCountAbort[pid][transLoad] + tempInstCountAbort[pid][transStore];
  instCount += tempInstCountAbort[pid][transInt] + tempInstCountAbort[pid][transFp] + tempInstCountAbort[pid][transBJ] + tempInstCountAbort[pid][transFence];
  
  if(printSummaryReport)
    summaryAbort(pid,instCount);

  if(transactionalReport)
    transactionalAbort(utid, instCount);


  nackingAddr[pid] = 0;
  nackingTimestamp[pid] = 0;
  nackingPid[pid] = -1;
  registerOut();
}



/*********************************************************
 ************* Transactional  Statistics *****************
 *********************************************************/

/**
 * @ingroup transReport
 * @brief   transactional report begin
 * 
 * @param utid    Unique Tx ID
 * @param tid  ThreadID
 * @param pid  Process ID
 * @param PC   Program counter
 * @param cpu  CPU ID
 * @param timestamp Internal time
 */
void transReport::transactionalBegin(ID utid, int tid, int pid, RAddr PC, int cpu, TIMESTAMP timestamp)
{
    //! We're starting a new transaction on the same process without a commit
    //! Last must have aborted (since we already filter out subsumed)
    if(activeTransactions.find(pid) != activeTransactions.end())
    {
      transDataReport.find(activeTransactions.find(pid)->second)->second.aborted = 1;
      transDataReport.find(activeTransactions.find(pid)->second)->second.endTimestamp = timestamp;

      transData tData = transDataReport.find(activeTransactions.find(pid)->second)->second;

      int readSetSize = tData.readSet.size();
      int writeSetSize = tData.writeSet.size();

      if ( printTransactionalReportDetail )
      {
        fprintf(outfile, "<Trans> tmReport:ABORT :%lld:%d:%d:%d:%0#10x:%d:%llu:%d:%llu:%d:%llu:%llu"
                                                ,tData.utid
                                                ,tData.pid
                                                ,tData.cpu
                                                ,tData.tid
                                                ,tData.PC
                                                ,1  // 1 Indicates that its an Abort
                                                ,tData.instCount
                                                ,readSetSize
                                                ,tData.reads
                                                ,writeSetSize
                                                ,tData.writes
                                                ,timestamp - tData.beginTimestamp
        );

        map<RAddr,INSTCOUNT>::iterator memSetIter;

        fprintf(outfile, ":");

        if(readSetSize != 0)
        {
          for(memSetIter = tData.readSet.begin(); memSetIter!=tData.readSet.end(); memSetIter++)
          {
            fprintf(outfile,"{%x,%llu}",memSetIter->first, memSetIter->second);
  //           if(--readSetSize>0)
  //             fprintf(outfile,",");
  
          }
        }
        else
          fprintf(outfile,"0");
  
        fprintf(outfile, ":");
  
        if(writeSetSize != 0)
        {
          for(memSetIter = tData.writeSet.begin(); memSetIter!=tData.writeSet.end(); memSetIter++)
          {
            fprintf(outfile,"{%x,%llu}",memSetIter->first,memSetIter->second);
  //           if(--writeSetSize>0)
  //             fprintf(outfile,",");
          }
        }
        else
          fprintf(outfile,"0");
  
        fprintf(outfile,":");
  
      // Grab non-useful stall count
      long long unsigned nackCyclePerTrans = 0;
  
        if(tData.conflicts.size() != 0)
        {
          list<conflict>::iterator it;
  
          for(it = tData.conflicts.begin(); it != tData.conflicts.end(); it++)
          {
            struct conflict tmpConflict = *it;
            fprintf(outfile,"{%d,%x,%llu}",tmpConflict.confPid,tmpConflict.raddr,tmpConflict.end - tmpConflict.begin);
            if ( tmpConflict.end - tmpConflict.begin <= 0 )
              nackCyclePerTrans++;
            else
              nackCyclePerTrans += tmpConflict.end - tmpConflict.begin;
          }
    
        }
        else
          fprintf(outfile,"0");
  
        fprintf(outfile, ":%d",0);
  
        fprintf(outfile, ":%llu",getCommittedInstCountbyCpu(tData.cpu));
  
        fprintf(outfile,":%llu",nackCyclePerTrans);
  
        fprintf(outfile, "\n");
  
        fflush(outfile);
    }


      activeTransactions.erase(tData.pid);

      if ( !printTransactionalReportSummary )
        transDataReport.erase(tData.utid);

    }

    struct transData temp;

    if(transDataReport.find(utid) != transDataReport.end())
    {
      temp = transDataReport.find(utid)->second;
    }
    else
    {
      temp.instCount = 0;
    }

    temp.utid = utid;
    temp.tid = tid;
    temp.aborted = 0;
    temp.pid = pid;
    temp.PC = PC;
    temp.cpu = cpu;
    temp.reads = 0;
    temp.writes = 0;
    temp.beginTimestamp = timestamp;
    temp.endTimestamp = 0;

    activeTransactions[pid]=utid;
    transDataReport[utid]=temp;

}
/**
 * @ingroup transReport
 * @brief   transactional report commit
 * 
 * @param utid    Unique Tx ID
 * @param instCount  Total instructions
 * @param timestamp Internal time
 * @param fpOps      Number of FP operations
 */
void transReport::transactionalCommit(ID utid, INSTCOUNT instCount, TIMESTAMP timestamp, INSTCOUNT fpOps)
{
   transData tData = transDataReport.find(utid)->second;
   transDataReport.find(utid)->second.endTimestamp = timestamp;
   transDataReport.find(utid)->second.instCount = instCount;

    tData.instCount = instCount;

    int readSetSize = tData.readSet.size();
    int writeSetSize = tData.writeSet.size();

      if ( printTransactionalReportDetail )
      {
        fprintf(outfile, "<Trans> tmReport:COMMIT:%lld:%d:%d:%d:%0#10x:%d:%llu:%d:%llu:%d:%llu:%llu"
                                                ,tData.utid
                                                ,tData.pid
                                                ,tData.cpu
                                                ,tData.tid
                                                ,tData.PC
                                                ,0  // 0 Indicates that its NOT an Abort
                                                ,tData.instCount
                                                ,readSetSize
                                                ,tData.reads
                                                ,writeSetSize
                                                ,tData.writes
                                                ,timestamp - tData.beginTimestamp
        );

      map<RAddr, INSTCOUNT>::iterator memSetIter;
  
      fprintf(outfile, ":");
  
      if(readSetSize != 0)
      {
        for(memSetIter = tData.readSet.begin(); memSetIter !=tData.readSet.end(); memSetIter++)
        {
          fprintf(outfile,"{%x,%llu}",memSetIter->first,memSetIter->second);
  //         if(--readSetSize>0)
  //           fprintf(outfile,",");
  
        }
      }
      else
        fprintf(outfile,"0");
  
  
      fprintf(outfile, ":");
  
      if(writeSetSize != 0)
      {
        for(memSetIter = tData.writeSet.begin(); memSetIter !=tData.writeSet.end(); memSetIter++)
        {
          fprintf(outfile,"{%x,%llu}",memSetIter->first,memSetIter->second);
  //         if(--writeSetSize>0)
  //           fprintf(outfile,",");
        }
      }
      else
        fprintf(outfile,"0");
  
      fprintf(outfile,":");
  
      // Grab useful stall count
      long long unsigned nackCyclePerTrans = 0;
  
      if(tData.conflicts.size() != 0)
      {
        list<conflict>::iterator it;
        for(it = tData.conflicts.begin(); it != tData.conflicts.end(); it++)
        {
          struct conflict tmpConflict = *it;
          fprintf(outfile,"{%d,%x,%llu}",tmpConflict.confPid,tmpConflict.raddr,tmpConflict.end - tmpConflict.begin);
  
          if ( tmpConflict.end - tmpConflict.begin <= 0 )
            nackCyclePerTrans ++;
          else
            nackCyclePerTrans += tmpConflict.end - tmpConflict.begin;
        }
  
      }
      else
        fprintf(outfile,"0");
  
      fprintf(outfile, ":%lli",fpOps);
  
      fprintf(outfile, ":%llu",getCommittedInstCountbyCpu(tData.cpu));


    fprintf(outfile,":%llu",nackCyclePerTrans);

    fprintf(outfile, "\n");
    fflush(outfile);
  }
    addToCommittedInstCountByCpu(tData.cpu, tData.instCount );
   activeTransactions.erase(tData.pid);

  if ( !printTransactionalReportSummary )
   transDataReport.erase(tData.utid);

}

/**
 * @ingroup transReport
 * @brief   transactional report abort
 * 
 * @param utid       Unique Tx ID
 * @param instCount  Total instructions
 */
void transReport::transactionalAbort(ID utid, INSTCOUNT instCount)
{
   transDataReport.find(utid)->second.instCount = instCount;
}

/**
 * @ingroup transReport
 * @brief   transactional report nack begin
 * 
 * @param utid       Unique Tx ID
 * @param tid        ThreadID
 * @param confPid    Conflicting process ID
 * @param raddr      Real address
 * @param timestamp  Internal time
 */
void transReport::transactionalNackBegin(ID utid, int tid, int confPid, RAddr raddr, TIMESTAMP timestamp)
{
    struct conflict temp;

    temp.begin = timestamp;
    temp.tid = tid;
    temp.raddr = raddr;
    temp.confPid = confPid;
    temp.end = 0;
    fflush(outfile);


    //! Since it's possible for a NK to appear before the Begin in the commit stage, we need
    //! to ensure that there is an entry in the transDataReport file before we add the conflict
    //! If there isn't one, we will create a very short temporary one.
    if(transDataReport.find(utid) != transDataReport.end())
    {
      transDataReport.find(utid)->second.conflicts.push_back(temp);
    }
    else
    {
      struct transData newTrans;
      newTrans.conflicts.push_back(temp);
      transDataReport[utid]=newTrans;
    }
}

/**
 * @ingroup transReport
 * @brief   transactional report nack finish
 * 
 * @param utid    Unique Tx ID
 * @param timestamp Internal time
 */
void transReport::transactionalNackFinish(ID utid, TIMESTAMP timestamp)
{
    transDataReport.find(utid)->second.conflicts.back().end = timestamp;
//     printf("TRANSACT:%d:%llu\n",transDataReport.find(utid)->second.pid,transDataReport.find(utid)->second.conflicts.back().end - transDataReport.find(utid)->second.conflicts.back().begin);fflush(stdout);
}

/**
 * @ingroup transReport
 * @brief   transactional report load
 * 
 * @param utid    Unique Tx ID
 * @param addr    Real address
 */
void transReport::transactionalLoad(ID utid, RAddr addr)
{
    transDataReport.find(utid)->second.reads++;
//     transDataReport.find(utid)->second.readSet.insert(addr);

    map<RAddr, INSTCOUNT>::iterator readSetIt = transDataReport.find(utid)->second.readSet.find(addr);
    if( readSetIt == transDataReport.find(utid)->second.readSet.end() )
    {
      int pid = transDataReport.find(utid)->second.pid;

      INSTCOUNT instCount = tempInstCount[pid][transLoad] + tempInstCount[pid][transStore];
      instCount += tempInstCount[pid][transInt] + tempInstCount[pid][transFp] + tempInstCount[pid][transBJ] + tempInstCount[pid][transFence];

      transDataReport.find(utid)->second.readSet[ addr ] = transDataReport.find(utid)->second.readSet[ addr ] = instCount;
    }


    if(calculateFullReadWriteSet)
      pReadSet.insert(addr);
}

/**
 * @ingroup transReport
 * @brief   transactional report store
 * 
 * @param utid    Unique Tx ID
 * @param addr    Real address
 */
void transReport::transactionalStore(ID utid, RAddr addr)
{
    transDataReport.find(utid)->second.writes++;
//     transDataReport.find(utid)->second.writeSet.insert(addr);

    map<RAddr, INSTCOUNT>::iterator writeSetIt = transDataReport.find(utid)->second.writeSet.find(addr);
    if( writeSetIt == transDataReport.find(utid)->second.writeSet.end() )
    {
      int pid = transDataReport.find(utid)->second.pid;

      INSTCOUNT instCount = tempInstCount[pid][transLoad] + tempInstCount[pid][transStore];
      instCount += tempInstCount[pid][transInt] + tempInstCount[pid][transFp] + tempInstCount[pid][transBJ] + tempInstCount[pid][transFence];

      transDataReport.find(utid)->second.writeSet[ addr ] = transDataReport.find(utid)->second.writeSet[ addr ] = instCount;
    }


    if(calculateFullReadWriteSet)
      pWriteSet.insert(addr);
}

/**
 * @ingroup transReport
 * @brief   transactional report cleanup
 * 
 */
void transReport::transactionalComplete()
{
    for ( unsigned int x = 0; x < MAX_CPU_COUNT; x++ )
    {
      if ( committedInstCountByCpu[x] > 0 )
      {
        fprintf(outfile, "<Trans> tmReport:ENDINST:%d:%lld", x,committedInstCountByCpu[x]);
        fprintf(outfile, "\n");
      }
    }

    if(calculateFullReadWriteSet)
    {
      set<RAddr>::iterator iter;
      int pReadSetSize = pReadSet.size();

      fprintf(outfile,"<Trans> tmReport:READ_SET:%d:",pReadSetSize);

      if(pReadSetSize > 0)
      {
        for(iter = pReadSet.begin(); iter!=pReadSet.end(); iter++)
        {
          fprintf(outfile,"%x",*iter);
          if(--pReadSetSize>0)
            fprintf(outfile,",");
        }

      }

      fprintf(outfile,"\n");

      int pWriteSetSize = pWriteSet.size();

      fprintf(outfile,"<Trans> tmReport:WRITE_SET:%d:", pWriteSetSize);

      if(pWriteSetSize > 0)
      {
        for(iter = pWriteSet.begin(); iter!=pWriteSet.end(); iter++)
        {
          fprintf(outfile,"%x",*iter);
          if(--pWriteSetSize>0)
            fprintf(outfile,",");
        }
      }

      fprintf(outfile,"\n");

    }

    fprintf(outfile, "<Trans> tmReport:INSTCOUNT:%lld:\n", ProcessId::nGradInsts);

    if ( printTransactionalReportSummary )
      transactionalCompleteSummary();
}

/**
 * @ingroup transReport
 * @brief   transactional report final summary output
 * 
 */
void transReport::transactionalCompleteSummary()
{

      unsigned long long commits[MAX_CPU_COUNT];
      unsigned long long aborts[MAX_CPU_COUNT];

      unsigned long long commitReads[MAX_CPU_COUNT];
      unsigned long long abortReads[MAX_CPU_COUNT];

      unsigned long long commitReadSet[MAX_CPU_COUNT];
      unsigned long long abortReadSet[MAX_CPU_COUNT];

      unsigned long long commitWrites[MAX_CPU_COUNT];
      unsigned long long abortWrites[MAX_CPU_COUNT];

      unsigned long long commitWriteSet[MAX_CPU_COUNT];
      unsigned long long abortWriteSet[MAX_CPU_COUNT];

      unsigned long long commitInst[MAX_CPU_COUNT];
      unsigned long long abortInst[MAX_CPU_COUNT];

      unsigned long long commitCycles[MAX_CPU_COUNT];
      unsigned long long abortCycles[MAX_CPU_COUNT];

      unsigned long long commitNackCycles[MAX_CPU_COUNT];
      unsigned long long abortNackCycles[MAX_CPU_COUNT];

      int x = 0;

      for ( x = 0; x < MAX_CPU_COUNT; x++ )
      {
        commits[x] = 0;
        aborts[x] = 0;
        commitReads[x] = 0;
        commitReadSet[x] = 0;
        commitWrites[x] = 0;
        commitWriteSet[x] = 0;
        commitInst[x] = 0;
        commitCycles[x] = 0;
        commitNackCycles[x] = 0;

        abortReads[x] = 0;
        abortReadSet[x] = 0;
        abortWrites[x] = 0;
        abortWriteSet[x] = 0;
        abortInst[x] = 0;
        abortCycles[x] = 0;
        abortNackCycles[x] = 0;
      }

      std::map<unsigned long long, transData>::iterator iter;
      std::list<conflict>::iterator confListIt;

      int pid;

      for ( iter = transDataReport.begin(); iter != transDataReport.end(); ++iter)
      {

        //! THIS LINE DEFINES WHAT WE CONSIDER THE "PID"
        pid = iter->second.cpu;

        if ( iter->second.aborted > 0 )
        {
          aborts[pid]++;
          abortReads[pid] += iter->second.reads;
          abortReadSet[pid] += iter->second.readSet.size();
          abortWrites[pid] += iter->second.writes;
          abortWriteSet[pid] += iter->second.writeSet.size();
          abortInst[pid] += iter->second.instCount;
          abortCycles[pid] += iter->second.endTimestamp - iter->second.beginTimestamp;
          for ( confListIt = iter->second.conflicts.begin(); confListIt != iter->second.conflicts.end(); ++confListIt)
          {
//             printf("TransPlace:A:%d:%llu\n",pid,(*confListIt).end - (*confListIt).begin);
            abortNackCycles[pid] += (*confListIt).end - (*confListIt).begin;
          }
        }
        else
        {
          commits[pid]++;
          commitReads[pid] += iter->second.reads;
          commitReadSet[pid] += iter->second.readSet.size();
          commitWrites[pid] += iter->second.writes;
          commitWriteSet[pid] += iter->second.writeSet.size();
          commitInst[pid] += iter->second.instCount;
          commitCycles[pid] += iter->second.endTimestamp - iter->second.beginTimestamp;
          for ( confListIt = iter->second.conflicts.begin(); confListIt != iter->second.conflicts.end(); ++confListIt)
          {
//             printf("TransPlace:C:%d:%llu\n",pid,(*confListIt).end - (*confListIt).begin);
            commitNackCycles[pid] += (*confListIt).end - (*confListIt).begin;
          }
        }

      }

  fprintf(outfile,"\n\n");
  fprintf(outfile,"<Trans> tmReportSummary:CPU:TX_COUNT:COMMITS:ABORTS:CM_INST:CM_CYCLES:CM_NKCYCLES:AVG_CM_INST:AVG_CM_CYC:AVG_CM_READS:AVG_CM_READSET:AVG_CM_WRITES:AVG_CM_WRITESET:AVG_CM_NACKCYC:AB_INST:AB_CYCLES:AB_NKCYCLES:AVG_AB_INST:AVG_AB_CYC:AVG_AB_READS:AVG_AB_READSET:AVG_AB_WRITES:AVG_AB_WRITESET:AVG_AB_NACKCYC\n");

  for ( x = 0; x < MAX_CPU_COUNT; x++ )
  {
    if ( aborts[x] + commits[x] > 0 )
    {
      fprintf(outfile,"<Trans> tmReportSummary:%d:%llu:%llu:%llu:%llu:%llu:%llu:%.4f:%.4f:%.4f:%.4f:%.4f:%.4f:%.4f:%llu:%llu:%llu:%.4f:%.4f:%.4f:%.4f:%.4f:%.4f:%.4f\n",
        x,
        commits[x] + aborts[x],
        commits[x],
        aborts[x],

        commitInst[x],
        commitCycles[x],
        commitNackCycles[x],
        (double)commitInst[x] / (double) commits[x],
        (double)commitCycles[x] / (double) commits[x],
        (double)commitReads[x] / (double) commits[x],
        (double)commitReadSet[x] / (double) commits[x],
        (double)commitWrites[x] / (double) commits[x],
        (double)commitWriteSet[x] / (double) commits[x],
        (double)commitNackCycles[x] / (double) commits[x],

        abortInst[x],
        abortCycles[x],
        abortNackCycles[x],
        (double)abortInst[x] / (double) aborts[x],
        (double)abortCycles[x] / (double) aborts[x],
        (double)abortReads[x] / (double) aborts[x],
        (double)abortReadSet[x] / (double) aborts[x],
        (double)abortWrites[x] / (double) aborts[x],
        (double)abortWriteSet[x] / (double) aborts[x],
        (double)abortNackCycles[x] / (double) aborts[x]);
    }
  }


  fprintf(outfile,"\n\n");
  return;
}

/*********************************************************
 ************* Global Summary Statistics *****************
 *********************************************************/

/**
 * @ingroup transReport
 * @brief   Handles begins for summary
 * 
 * @param pid  Process ID
 * @param timestamp Internal time
 *
 * Used For: Global Summary Statistics
 * Called on a Begin to store the current timestamp as well as clear all
 * of the counters.
 */
void transReport::summaryBegin(int pid, TIMESTAMP timestamp)
{

  /**
   * @note
   *  If our summaryTransFlag is set, this means that we were
   *  currently in a transaction and received another Begin.  This
   *  implies the previous transaction aborted, thus we can take
   *  abort cycle counts.
   *
   *  This does NOT increment the abort count, because to
   *  get the instruction counts we have an explicit Abort function
   *  that increments the count.
   *
   *  Also Note:  This will have to be modified if we eventually
   *  support nested transactions.
  */

  if(summaryTransFlag[pid]==1)
  {

    unsigned long long cycleCount = timestamp - summaryBeginCycle[pid];
    summaryAbortCycleCount += cycleCount;
    
    if(cycleCount < summaryMinAbortCycleCount)
      summaryMinAbortCycleCount = cycleCount;
    if(cycleCount > summaryMaxAbortCycleCount)
      summaryMaxAbortCycleCount = cycleCount;
  }
  

  summaryBeginCycle[pid] = timestamp;
  summaryReadSet[pid].clear();
  summaryWriteSet[pid].clear();
  tempLoadCount[pid] = 0;
  tempStoreCount[pid] = 0;
  summaryTransFlag[pid]=1;

//  Commented this out because it needs to be handled by Abort/Commit because NACKs can happen before begin
//   summaryNackCount[pid]=0;
//   summaryNackCycleCount[pid]=0;

}

/**
 * @ingroup transReport
 * @brief   Handles commits for summary
 * 
 * @param pid  Process ID
 * @param instCount  Total instructions
 * @param timestamp Internal time
 *
 *  Used For: Global Summary Statistics
 *  Called on a Commit to increment the Commit count as well as the
 *  instruction counts for commits for that transaction.  Also calculates
 *  the length of the transaction in cycles.  Note, we also only
 *  count read/write set counts here because aborted transactions don't
 *  give an accurate view of the total read/write set size (since they
 *  do not complete).
 */
void transReport::summaryCommit(int pid, INSTCOUNT instCount, TIMESTAMP timestamp)
{
  summaryCommitCount++;
  unsigned long long cycleCount = timestamp - summaryBeginCycle[pid];
  summaryCommitCycleCount += cycleCount;

  if(cycleCount < summaryMinCommitCycleCount)
    summaryMinCommitCycleCount = cycleCount;
  if(cycleCount > summaryMaxCommitCycleCount)
    summaryMaxCommitCycleCount = cycleCount;

  if(instCount < summaryMinCommitInstCount)
    summaryMinCommitInstCount = instCount;
  if(instCount > summaryMaxCommitInstCount)
    summaryMaxCommitInstCount = instCount;

  summaryCommitInstCount += instCount;

  summaryReadSetSize += summaryReadSet[pid].size();
  summaryWriteSetSize += summaryWriteSet[pid].size();
  summaryLoadCount += tempLoadCount[pid];
  summaryStoreCount += tempStoreCount[pid];

  summaryTransFlag[pid]=0;

  summaryUsefulNackCount += summaryNackCount[pid];
   summaryNackCount[pid] = 0;
  summaryUsefulNackCycle += summaryNackCycleCount[pid];
   summaryNackCycleCount[pid] = 0;

}

/**
 * @ingroup transReport
 * @brief   Handles aborts for summary
 * 
 * @param pid  Process ID
 * @param instCount  Total instructions
 *
 * Used For: Global Summary Statistics
 * Called on an AB!! to increment the abort count, as well as incrementing
 * the instruction counts for aborted instructions.  Does not handle cycle
 * information, that is handled in Begin for timing accurate results.
 */
void transReport::summaryAbort(int pid, INSTCOUNT instCount)
{
  summaryAbortCount++;
  summaryAbortInstCount += instCount;

  summaryAbortedNackCount += summaryNackCount[pid];
   summaryNackCount[pid] = 0;
  summaryAbortedNackCycle += summaryNackCycleCount[pid];

   summaryNackCycleCount[pid] = 0;



  if(instCount < summaryMinAbortInstCount)
    summaryMinAbortInstCount = instCount;
  if(instCount > summaryMinAbortInstCount)
    summaryMaxAbortInstCount = instCount;

}

/**
 * @ingroup transReport
 * @brief   Handles NACK begins for summary
 * 
 * @param pid  Process ID
 * @param timestamp Internal time
 *
 * Used For: Global Summary Statistics
 * Called at the begining of a NK so that we can record the start time
 * and thus calulate the total length of the stall on the NKFN. Also
 * increments the Nack Count for that transaction.
 */
void transReport::summaryNackBegin(int pid, TIMESTAMP timestamp)
{
  summaryNackCycle[pid] = timestamp;
  summaryNackCount[pid]++;
}

/**
 * @ingroup transReport
 * @brief   global report load
 * 
 * @param pid  Process ID
 * @param addr Real address
 *
 * Used For: Global Summary Statistics
 * Called on a load, adds the address to a write set for the transaction
 * and increments the load count for the transaction.
 */
void transReport::summaryLoad(int pid, RAddr addr)
{
  summaryReadSet[pid].insert(addr);
  tempLoadCount[pid]++;
}

/**
 * @ingroup transReport
 * @brief   global report store
 * 
 * @param pid  Process ID
 * @param addr Real address
 *
 * Used For: Global Summary Statistics
 * Called on a store, adds the address to a write set and
 * increments the store count for the transaction.
 */
void transReport::summaryStore(int pid, RAddr addr)
{
  summaryWriteSet[pid].insert(addr);
  tempStoreCount[pid]++;
}

/**
 * @ingroup transReport
 * @brief   global report nack finish
 * 
 * @param pid  Process ID
 * @param timestamp Internal time
 *
 * Used For: Global Summary Statistics
 * Called on a NKFN to calculate the length of the Nack stall
 * defaults to a minimum value of 1.
 */
void transReport::summaryNackFinish(int pid, TIMESTAMP timestamp)
{

  unsigned long long cycleCount = timestamp - summaryNackCycle[pid];
//   if(cycleCount == 0)
//     cycleCount = 1;

//   printf("SUMMARY PID:%d:%llu\n",pid,cycleCount);fflush(stdout);
  
  summaryNackCycleCount[pid] += cycleCount;

}

/**
 * @ingroup transReport
 * @brief   global report output final results
 *
 * Used For: Global Summary Statistics
 * Printed at the end of execution to summarize global transactional results.
 */
void transReport::summaryComplete()
{
  if(printSummaryReport)
  {
    // If we are also doing a detailed trace, send END string 
   if(printDetailedTrace)
      fprintf(outfile, "<Trans> tmTrace: END   :99999999999:666::\n");

        fprintf(outfile, "#tableG,ALL,ALL,StatsInc,Commit,Abort,");
        fprintf(outfile, "NTot,NAvg,NCyc,NCycAvg,");
        fprintf(outfile, "ComCycTot,ComCycAvg,ComCycMin,ComCycMax,");

        fprintf(outfile, "InstTotCm,InstAvgCm,InstMinCm,InstMaxCm,");
        fprintf(outfile, "AbortCycTot,AbortCycAvg,AbortCycMin,AbortCycMax,");
        fprintf(outfile, "InstTotAb,InstAvgAb,InstMinAb,InstMaxAb,");
        fprintf(outfile, "R-Set,Loads,W-Set,Stores,BeginInstCount,BeginCycleCount,");

        fprintf(outfile, "ANTot,ANAvg,ANCyc,ANCycAvg\n");

        fprintf(outfile, "tableG,ALL,ALL,%llu,%llu,%llu,%llu,%.2f,%llu,%.2f,%llu,%.2f,%llu,%llu,%llu,%.2f,%llu,%llu,%llu,%.2f,%llu,%llu,%llu,%.2f,%llu,%llu,%.2f,%.2f,%.2f,%.2f,%llu,%llu,%llu,%.2f,%llu,%.2f\n",

                summaryAbortCount + summaryCommitCount,
                summaryCommitCount,
                summaryAbortCount,

         //! Global statistics not reliable for parsing between useful/aborted nacks, just total

                summaryUsefulNackCount + summaryAbortedNackCount,
                (( float )( summaryUsefulNackCount + summaryAbortedNackCount) / ( float )(summaryCommitCount + summaryAbortCount) ),
                (summaryUsefulNackCycle + summaryAbortedNackCycle),
                (( float)(summaryUsefulNackCycle + summaryAbortedNackCycle) / (float) ( summaryUsefulNackCount + summaryAbortedNackCount)),   

                summaryCommitCycleCount,
               (( float )summaryCommitCycleCount / ( float )summaryCommitCount ),
                summaryMinCommitCycleCount,
                summaryMaxCommitCycleCount,

                summaryCommitInstCount,
                (( float )summaryCommitInstCount / ( float )summaryCommitCount ),
                summaryMinCommitInstCount,
                summaryMaxCommitInstCount,

                summaryAbortCycleCount,
                (( float )summaryAbortCycleCount / ( float )summaryAbortCount ),
                summaryMinAbortCycleCount,
                summaryMaxAbortCycleCount,

                summaryAbortInstCount,
                (( float )summaryAbortInstCount / ( float )summaryAbortCount ),
                summaryMinAbortInstCount,
                summaryMaxAbortInstCount,

                (( float )summaryReadSetSize / ( float )summaryCommitCount ),
                (( float )summaryLoadCount / ( float )summaryCommitCount ),
                (( float )summaryWriteSetSize / ( float )summaryCommitCount ),
                (( float )summaryStoreCount / ( float )summaryCommitCount ),
                beginRecordkeepingInstructionCount,
                beginRecordKeepingCycleCount,

                summaryAbortedNackCount,
                (( float )summaryAbortedNackCount / ( float )(summaryAbortCount) ),
                summaryAbortedNackCycle,
                (( float)summaryAbortedNackCycle / (float) summaryAbortedNackCount)

                );



    fprintf(outfile, "\nGlobal Totals:\n" );
    fprintf(outfile, "          Trans ->   StatsInc: %7llu    Commit: %9llu    Abort: %8llu\n",
            summaryAbortCount + summaryCommitCount,
            summaryCommitCount,
            summaryAbortCount);

   //! Global statistics not reliable for parsing between useful/aborted nacks, just total

    fprintf(outfile,"          Nacks ->   Total:  %9llu    Avg:   %10.2f    Cyc: %10llu    Avg:   %9.2f\n",
            (summaryUsefulNackCount + summaryAbortedNackCount),
            (( float )( summaryUsefulNackCount + summaryAbortedNackCount) / ( float )(summaryCommitCount + summaryAbortCount) ),
            (summaryUsefulNackCycle + summaryAbortedNackCycle),
            (( float)( summaryUsefulNackCycle + summaryAbortedNackCycle)/ (float) ( summaryUsefulNackCount + summaryAbortedNackCount )));

//     fprintf(outfile,"          A_Nack->   Total:  %9llu    Avg:   %10.2f    Cyc: %10llu    Avg:   %9.2f\n",
//             summaryAbortedNackCount,
//             (( float )summaryAbortedNackCount / ( float )(summaryAbortCount) ),
//             summaryAbortedNackCycle,
//             (( float)summaryAbortedNackCycle / (float) summaryAbortedNackCount));


    fprintf(outfile,"          CyclCM->   Total:  %9llu    Avg:   %10.2f    Min:   %8llu    Max:    %8llu\n",
            summaryCommitCycleCount,
            (( float )summaryCommitCycleCount / ( float )summaryCommitCount ),
            summaryMinCommitCycleCount,
            summaryMaxCommitCycleCount );
    fprintf(outfile,"          InstCM->   Total:  %9llu    Avg:   %10.2f    Min:   %8llu    Max:    %8llu\n",
            summaryCommitInstCount,
            (( float )summaryCommitInstCount / ( float )summaryCommitCount ),
            summaryMinCommitInstCount,
            summaryMaxCommitInstCount );
    fprintf(outfile,"          CyclAB->   Total:  %9llu    Avg:   %10.2f    Min:   %8llu    Max:    %8llu\n",
            summaryAbortCycleCount,
            (( float )summaryAbortCycleCount / ( float )summaryAbortCount ),
            summaryMinAbortCycleCount,
            summaryMaxAbortCycleCount );
    fprintf(outfile,"          InstAB->   Total:  %9llu    Avg:   %10.2f    Min:   %8llu    Max:    %8llu\n",
            summaryAbortInstCount,
            (( float )summaryAbortInstCount / ( float )summaryAbortCount ),
            summaryMinAbortInstCount,
            summaryMaxAbortInstCount );
    fprintf(outfile,"          Memory->   R-Set:  %9.2f    Loads: %10.2f    W-Set: %8.2f    Stores: %8.2f\n\n",
            (( float )summaryReadSetSize / ( float )summaryCommitCount ),
            (( float )summaryLoadCount / ( float )summaryCommitCount ),
            (( float )summaryWriteSetSize / ( float )summaryCommitCount ),
            (( float )summaryStoreCount / ( float )summaryCommitCount ) );
  }

  fflush(outfile);
}


/*****************************************************
 ************* transMemRef Functions *****************
 *****************************************************/

/**
 * @ingroup transReport
 * @brief   transMemRef store function
 * 
 * @param pid  Process ID
 * @param PC   Program counter
 *
 * Function called on new Begin instruction.  First checks to see
 * if the map has an entry for this PC.  If it does not, it adds a
 * new list of lists of RAddrs for that PC.  Otherwise it simply
 * adds a new list of RAddrs into the current list at that PC.
 * Note:  It must also ensure the previous transaction wasn't
 * aborted.  If it was, it simply clears the RAddrs from the
 * list and continues.
 */
void transReport::transMemRef_newBegin(int pid, RAddr PC)
{
  //! Ensure that we have experienced a commit since the last begin
  //! for this pid (this ensures the previous transaction on
  //! this pid wasn't aborted).
  if(transMemRefState[pid] == 0)
  {
    //! Set the "running" state to true so that we can detect an abort
    transMemRefState[pid] = 1;

    //! First we will take care of LOADs

    map < RAddr, list < list < RAddr > > >::iterator ldIt;
    ldIt = transMemHistoryLoads.find(PC);

    //! If the PC isn't currently in our map, add an entry for it
    if(ldIt == transMemHistoryLoads.end())
    {
      list< RAddr > newLdMemList;
      list< list < RAddr > > newLdTransList;
      newLdTransList.push_back(newLdMemList);
      transMemHistoryLoads[PC] = newLdTransList ;
    }
    //! Otherwise, simply create a new list and add it to the current
    else
    {
      list< RAddr > newLdMemList;
      ldIt->second.push_back(newLdMemList);
    }

    //! Now we take care of the STOREs list
    map < RAddr, list < list < RAddr > > >::iterator stIt;
    stIt = transMemHistoryStores.find(PC);

    //! If the PC isn't currently in our map, add an entry for it
    if(stIt == transMemHistoryStores.end())
    {
      list< RAddr > newStMemList;
      list< list < RAddr > > newStTransList;
      newStTransList.push_back(newStMemList);
      transMemHistoryStores[PC] = newStTransList ;
    }
    //! Otherwise, simply create a new list and add it to the current
    else
    {
      list< RAddr > newStMemList;
      stIt->second.push_back(newStMemList);
    }
  }
  //! If we have not encountered a commit, then the previous transaction
  //! was aborted.  Since we do not want to keep track of aborted transactions
  //! simply clear out the current list of RAddrs and continue
  else
  {
    transMemHistoryStores.find(PC)->second.back().clear();
    transMemHistoryLoads.find(PC)->second.back().clear();
  }
}

/**
 * @ingroup transReport
 * @brief   transMemRef commit
 * 
 * @param pid  Process ID
 *
 * Since we only want to add to our map memory addresses for
 * completed transactions, this is used to clear a flag that
 * allows us to detect aborted transactions.  Note subsumed
 * transactions are detected elsewhere, so they are not an
 * issue.
 */
void transReport::transMemRef_newCommit(int pid)
{
  transMemRefState[pid] = 0;
}

/**
 * @ingroup transReport
 * @brief   transMemRef load 
 * 
 * @param PC   Program counter
 * @param mem  Real address
 *
 * On a load we simply push the memory address onto the
 * current list
 */
void transReport::transMemRef_newLoad(RAddr PC, RAddr mem)
{
  I(transMemHistoryLoads.find(PC) != transMemHistoryLoads.end());
  transMemHistoryLoads.find(PC)->second.back().push_back(mem);
}

/**
 * @ingroup transReport
 * @brief   transMemRef store
 * 
 * @param PC   Program counter
 * @param mem  Real address
 *
 * On a Store we simply push the memory address onto the
 * current list
 */
void transReport::transMemRef_newStore(RAddr PC, RAddr mem)
{
  I(transMemHistoryStores.find(PC) != transMemHistoryStores.end());
  transMemHistoryStores.find(PC)->second.back().push_back(mem);
}

/**
 * @ingroup transReport
 * @brief   Prints out the entire map.  Useful for validation.
 *
 */
void transReport::transMemRef_printResults()
{
  int x=0;
  printf("\n\ntransMemRef Results:\n\n");

  printf("Loads: \n");
  map < RAddr, list < list < RAddr > > >::iterator pcIt;
  for(pcIt = transMemHistoryLoads.begin(); pcIt != transMemHistoryLoads.end(); pcIt++)
  {
    printf("\n");
    printf("\tPC %0#10x:\n", pcIt->first);
    list< list <RAddr> >::iterator transIt;
    for(transIt = pcIt->second.begin(); transIt != pcIt->second.end(); transIt++)
    {
      x++;
      printf("\n\n");
      list<RAddr>::iterator memIt;
      for(memIt = transIt->begin(); memIt != transIt->end(); memIt++)
      {
        printf("%d %0#10x\n",x,*memIt);
      }
    }
  }

  printf("\n\nStores: \n");
  map < RAddr, list < list < RAddr > > >::iterator pcStIt;
  for(pcStIt = transMemHistoryStores.begin(); pcStIt != transMemHistoryStores.end(); pcStIt++)
  {
    printf("\n");
    printf("\tPC %0#10x:\n", pcStIt->first);
    list< list <RAddr> >::iterator transStIt;
    for(transStIt = pcStIt->second.begin(); transStIt != pcStIt->second.end(); transStIt++)
    {
      x++;
      printf("\n\n");
      list<RAddr>::iterator memStIt;
      for(memStIt = transStIt->begin(); memStIt != transStIt->end(); memStIt++)
      {
        printf("%d %0#10x\n",x,*memStIt);
      }
    }

  }
}

/**
 * @ingroup transReport
 * @brief   Returns the transMemHistoryLoads map (L).
 * 
 * @return  History of memory operations (L)
 */
map < RAddr, list < list < RAddr > > > transReport::transMemRef_getLoadMap()
{
  return transMemHistoryLoads;
}

/*
 * Function: transMemRef_getStoreMap()
 * Returns the transMemHistoryStores map.
*/

/**
 * @ingroup transReport
 * @brief   Returns the transMemHistoryStores map (S).
 * 
 * @return  History of memory operations (S)
 */
map < RAddr, list < list < RAddr > > > transReport::transMemRef_getStoreMap()
{
  return transMemHistoryStores;
}

/**
 * @ingroup transReport
 * @brief   Returns the list of lists of memory references for a given PC address
 * 
 * @param PC   Program counter
 * @return     History of memory operations (L)
 */
std::list < list < RAddr > > transReport::transMemRef_getLoadLists(RAddr PC)
{
  map < RAddr, list < list < RAddr > > >::iterator iter;
  iter = transMemHistoryLoads.find(PC);
  if(iter != transMemHistoryLoads.end())
  {
    return iter->second;
  }
  else
  {
    list < list <RAddr> > emptyList;
    return emptyList;
  }
}

/**
 * @ingroup transReport
 * @brief   Returns the list of lists of memory references for a given PC address
 * 
 * @param PC   Program counter
 * @return     History of memory operations (S)
 */
std::list < list < RAddr > > transReport::transMemRef_getStoreLists(RAddr PC)
{
  map < RAddr, list < list < RAddr > > >::iterator iter;
  iter = transMemHistoryStores.find(PC);
  if(iter != transMemHistoryStores.end())
  {
    return iter->second;
  }
  else
  {
    list < list <RAddr> > emptyList;
    return emptyList;
  }
}

map < RAddr, list < list < RAddr > > > *transReport::transMemRef_ref_getLoadMap()
{
  return &transMemHistoryLoads;
}

map < RAddr, list < list < RAddr > > > *transReport::transMemRef_ref_getStoreMap()
{
  return &transMemHistoryStores;
}

/**
 * @ingroup transReport
 * @brief   transMemRef get load addresses for PC
 * 
 * @param PC   Program counter
 * @return 
 */
std::list < list < RAddr > > *transReport::transMemRef_ref_getLoadLists(RAddr PC)
{
  map < RAddr, list < list < RAddr > > >::iterator iter;
  iter = transMemHistoryLoads.find(PC);
  if(iter != transMemHistoryLoads.end())
  {
    return &iter->second;
  }
  else
  {
    return &emptyList;
  }
}

/**
 * @ingroup transReport
 * @brief   transMemRef get store list for PC
 * 
 * @param PC   Program counter
 * @return 
 */
std::list < list < RAddr > > *transReport::transMemRef_ref_getStoreLists(RAddr PC)
{
  map < RAddr, list < list < RAddr > > >::iterator iter;
  iter = transMemHistoryStores.find(PC);
  if(iter != transMemHistoryStores.end())
  {
    return &iter->second;
  }
  else
  {
    return &emptyList;
  }
}

/**
 * @ingroup transReport
 * @brief   get global read set
 * 
 * @return
 */
std::set<RAddr> transReport::return_globalReadSet()
{
   return this->pReadSet;
}

/**
 * @ingroup transReport
 * @brief   get global write set
 * 
 * @return
 */
std::set<RAddr> transReport::return_globalWriteSet()
{
   return this->pWriteSet;
}

/**
 * @ingroup transReport
 * @brief   Sets the begining Instruction/Cycle count of transactional workloads
 * 
 * @param insts instruction count
 */
void transReport::beginTMStats(INSTCOUNT insts)
{
  if((beginRecordkeepingInstructionCount==0) && recordTMStart)
  {
    beginRecordkeepingInstructionCount=insts;
    beginRecordKeepingCycleCount=globalClock;
    fprintf(outfile,"<Trans> tmTrace: BRCD :99999999999:0:6666:%llu:%llu\n",
      beginRecordkeepingInstructionCount,
      beginRecordKeepingCycleCount);
  }
  else
  {
    fprintf(outfile,"<Trans> tmTrace: IRCD :99999999999:0:6666:%llu:%llu\n",
      insts,
      globalClock);
  }
}

void transReport::incrementCommittedInstCountByCpu( int cpu )
{
  committedInstCountByCpu[cpu]++;
}

void transReport::addToCommittedInstCountByCpu( int cpu, INSTCOUNT count )
{
  committedInstCountByCpu[cpu] += count;
}

INSTCOUNT transReport::getCommittedInstCountbyCpu( int cpu )
{
  return committedInstCountByCpu[cpu];
}

void transReport::reportBarrier ( int pid )
{
  if(transactionalReport)
  {
    if ( printTransactionalReportDetail )
    {
      fprintf(outfile, "<Trans> tmReport:BARRIER:%d:%llu:%llu\n"
                                              ,pid
                                              ,committedInstCountByCpu[pid]
                                              ,globalClock);
    }
  }
}

