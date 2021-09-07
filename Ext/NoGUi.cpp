#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN   // Exclude not needed stuff from Windows headers.
#endif

//OS Specific
#include <windows.h>

//C++ Specific
#include <iostream>
#include <chrono>
#include <string>
#include <random>


//C Specific
#include <stdlib.h> //VC++'s C runtime is multithreaded by default. There's no need for rand_r, rand works fine in this case.
#include <stdint.h>
#include <time.h>


#define MSG_BUFFER_SIZE 500
#define NUM_PRODUCERS 1
#define NUM_PROGRESS_MONITORS 1

//array of all thread IDs
DWORD * threadIds;

//finished flag
bool done = false;

//sleeping condition variables
HANDLE producerSemaphore;
HANDLE senderSemaphore;

//msg buffer variables
HANDLE bufferMutex;

char msgBuffer[MSG_BUFFER_SIZE][101];

uint64_t msgsInBuffer = 0;
uint64_t startMsgIndex = 0;
uint64_t endMsgIndex = 0;


//output stats mutex
HANDLE statsMutex;
uint64_t msgsSent = 0;
uint64_t msgsFailed = 0;
int64_t sleepAmtAccum = 0;


typedef struct ProducerInfo
{
	uint64_t numMsgs = 1000;
} ProducerInfo;

typedef struct SenderInfo
{
	uint64_t meanMilliSecondWaitTime = 0; 
	double standardDevWaitTime = 1000.0; //default standard deviation is +- 1 sec
	double failureRate = 0.5;
} SenderInfo;

//I did not implement this usleep, got it from https://www.c-plusplus.net/forum/topic/109539/usleep-unter-windows/3
void usleep(int64_t usec) 
{ 
    HANDLE timer; 
    LARGE_INTEGER ft; 

    ft.QuadPart = -(10*usec); // Convert to 100 nanosecond interval, negative value indicates relative time

    timer = CreateWaitableTimer(NULL, TRUE, NULL); 
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0); 
    WaitForSingleObject(timer, INFINITE); 
    CloseHandle(timer); 
}

inline 
void GenRandomMsg100(char msgBuffer[])
{
	for(int i = 0; i < 100; ++i)
	{
		//range of good chars 127 - 32 + 1 = 96. It starts at 32 so + 32
		msgBuffer[i] = (char) (rand()%96+32); 
	}
	msgBuffer[100] = '\0';
}


DWORD WINAPI Producer(LPVOID param) 
{ 
	ProducerInfo* pInfo = (ProducerInfo*) param;
	for(int msg = 0; msg < pInfo->numMsgs; ++msg)
	{
		WaitForSingleObject(producerSemaphore,INFINITE);

		WaitForSingleObject(bufferMutex, INFINITE);
		GenRandomMsg100(msgBuffer[endMsgIndex]);
		endMsgIndex = (endMsgIndex+1) % MSG_BUFFER_SIZE;
		++msgsInBuffer;
		ReleaseMutex(bufferMutex);
		ReleaseSemaphore(senderSemaphore, 1, NULL);
	}
	done = true;
	return 0;
}

inline
void ProcessMsg(SenderInfo* sInfo, std::mt19937 & generator, std::normal_distribution<double> & waitTimeDistribution, std::uniform_real_distribution<double> & msgFailDistribution)
{
	int64_t sleepAmt = waitTimeDistribution(generator);
	if(sleepAmt < 0.0)
		sleepAmt = 0.0;
	usleep(sleepAmt);
	bool failed = (msgFailDistribution(generator) < sInfo->failureRate);

	WaitForSingleObject(statsMutex, INFINITE);
	sleepAmtAccum += sleepAmt;
	if(failed)
	{
		++msgsFailed;
	}
	else
	{
		++msgsSent;
	}
	ReleaseMutex(statsMutex);
}


DWORD WINAPI Sender(LPVOID param) 
{ 
	
	char msg[101];
	msg[100] ='\0';
	SenderInfo* sInfo = (SenderInfo*) param;
	static thread_local std::mt19937 generator(time(NULL));
  	std::normal_distribution<double> waitTimeDistribution(sInfo->meanMilliSecondWaitTime,sInfo->standardDevWaitTime);
	std::uniform_real_distribution<double> msgFailDistribution(0.0,1.0);

	while(!done)
	{
		DWORD waitResult = WaitForSingleObject(senderSemaphore,0L);
		switch(waitResult)
		{
			case WAIT_TIMEOUT:
			{
				continue;
			}
			default:
				break;
		}

		WaitForSingleObject(bufferMutex, INFINITE);
		memcpy(&msg[0], &msgBuffer[startMsgIndex][0], 100);
		startMsgIndex = (startMsgIndex+1) % MSG_BUFFER_SIZE;
		--msgsInBuffer;


		ReleaseMutex(bufferMutex);

		ReleaseSemaphore(producerSemaphore, 1, NULL);

		ProcessMsg(sInfo, generator, waitTimeDistribution, msgFailDistribution);

	}

	//clean up loop, no sync with producer needed here
	WaitForSingleObject(bufferMutex, INFINITE);
	while(msgsInBuffer != 0)
	{

		memcpy(&msg[0], &msgBuffer[startMsgIndex][0], 100);
		startMsgIndex = (startMsgIndex+1) % MSG_BUFFER_SIZE;
		--msgsInBuffer;


		ReleaseMutex(bufferMutex);

		ProcessMsg(sInfo, generator, waitTimeDistribution, msgFailDistribution);

		WaitForSingleObject(bufferMutex, INFINITE);
	}
	ReleaseMutex(bufferMutex);
	return 0;
}

DWORD WINAPI ProgressMonitor(LPVOID param) 
{ 
	return 0;
}



int main(int argc, char* argv[])
{
	srand(time(NULL)); //seed randomness


	ProducerInfo* pInfo = new ProducerInfo();

	uint32_t numSenders = 2;
	std::vector<HANDLE> threadHandles;
	threadIds = new DWORD[NUM_PRODUCERS+NUM_PROGRESS_MONITORS+numSenders]; //bulk allocate ids

	bufferMutex = CreateMutexA(NULL, 			    //default security attributes
  							   true, 			 	//does thread calling it auto get the mutex
  							   "Msg Buffer Mutex"); //mutex name

	statsMutex = CreateMutexA(NULL, 			    //default security attributes
  						      true, 			 	//does thread calling it auto get the mutex
  						      "Stats Mutex");       //mutex name

	producerSemaphore =	CreateSemaphoreA(NULL,
  										 500, 				 //init count
  										 500,				 //max count
  										 "Producer Sema");
	senderSemaphore =	CreateSemaphoreA(NULL,
  										 0, 				 //init count
  										 500,				 //max count
  										 "Sender Sema");


	//Create Produce thread
	threadHandles.push_back(CreateThread(NULL,           	 // default security attributes
        	 							 0,               	 // use default stack size  
             						  	 Producer,   	 	 // thread function name
             							 pInfo,           	 // argument to thread function 
             							 0,               	 // use default creation flags 
             							 threadIds)); 

	//Create Produce thread
	threadHandles.push_back(CreateThread(NULL,           	 // default security attributes
        	 							 0,               	 // use default stack size  
             						  	 ProgressMonitor,    // thread function name
             							 nullptr,            // argument to thread function 
             							 0,               	 // use default creation flags 
             							 threadIds+NUM_PRODUCERS)); 

	//Allow each Sender to be customizable
	SenderInfo* sInfo = new SenderInfo[numSenders];

	//Create Senders (Consumers) thread
	for(int i = 0; i < numSenders; ++i)
	{
		threadHandles.push_back(CreateThread(NULL,           	 // default security attributes
         							 		 0,               	 // use default stack size  
            						  	 	 Sender,   	 	 	 // thread function name
            								 &sInfo[i],          // argument to thread function 
            							 	 0,               	 // use default creation flags 
            							 	 threadIds+NUM_PRODUCERS+NUM_PROGRESS_MONITORS+i)); 
	}


	ReleaseMutex(bufferMutex);	
	ReleaseMutex(statsMutex);
	//simulation starts now

	WaitForMultipleObjects(threadHandles.size(), &threadHandles[0], true, INFINITE);
	delete [] sInfo;
	delete pInfo;
	delete [] threadIds;

	std::cout << "Stats:" << std::endl;
	std::cout << "Sent: " << msgsSent << std::endl;
	std::cout << "Failed: " << msgsFailed << std::endl;
	std::cout << "Average time: " << sleepAmtAccum/1000.0 << std::endl;

	CloseHandle(bufferMutex);
	CloseHandle(statsMutex);
	CloseHandle(producerSemaphore);
	CloseHandle(senderSemaphore);
	for(int i=0; i < threadHandles.size(); ++i)
    {
    	CloseHandle(threadHandles[i]);
    }

	return 0;
}



