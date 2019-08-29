/*!***************************************************************************
*!
*! FILE NAME  : tcpsocket.cc
*!
*! DESCRIPTION: TCP Socket
*!
*!***************************************************************************/

/****************** INCLUDE FILES SECTION ***********************************/

#include "compiler.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
extern "C"
{
#include "system.h"
#include "timr.h"
}

#include "iostream.hh"
#include "tcp.hh"
#include "ip.hh"
#include "tcpsocket.hh"
#include "threads.hh"



//#define D_TCP
#ifdef D_TCP
#define trace cout
#else
#define trace if(false) cout
#endif
/****************** TCPSocket DEFINITION SECTION *************************/

//----------------------------------------------------------------------------
//

TCPSocket::TCPSocket(TCPConnection* theConnection):
myConnection(theConnection),
myReadSemaphore(Semaphore::createQueueSemaphore("Read", 0)),
myWriteSemaphore(Semaphore::createQueueSemaphore("Write", 0))
{

}
/*
The application is blocked on the call mySocket->Read due to the semaphore associated
with the method. The state ESTABLISHED in the TCP state machine implies that
when a segment with the PSH flag set is received,
the data in the segment should be passed to the application as soon as possible.
Thus, the method TCPSocket::socketDataReceived will be invoked in
the method EstablishedState::Receive and let the application
leave the wait statement and handle the data.


*/
// Read called in doit method (the applications main while loop)

TCPSocket::~TCPSocket(){


  delete myReadSemaphore;
  delete myWriteSemaphore;

}

byte*
TCPSocket::Read(udword& theLength) // Copied from the lab manual
{
  myReadSemaphore->wait(); // Wait for available data
  theLength = myReadLength;
  byte* aData = myReadData;
  myReadLength = 0;
  myReadData = 0;
  return aData;
}

// Called in establishedstate->receive and will let the application leave the wait
void
TCPSocket::socketDataReceived(byte* theData, udword theLength) // Copied from the lab manual
{

  myReadData = new byte[theLength];
  memcpy(myReadData, theData, theLength);
  myReadLength = theLength;
  myReadSemaphore->signal(); // Data is available
}

// write is called in doit method (the applications main while loop)
// Eacho's the data and block until socketdatasent unlblocks with signal()
void
TCPSocket::Write(byte* theData, udword theLength)
{
  if(myConnection->RSTFlag){ // we received a RSTflag which is received when closing
    return; // Makes sure it's not called.
  }
  myConnection->Send(theData, theLength);
  myWriteSemaphore->wait(); // Wait until the data is acknowledged
}

/*
The method TCPSocket::socketDataSent should only be invoked
from the state machine when all data are sent and acknowledged
by the recipient = in acknowledge in established state.
*/

void
TCPSocket::socketDataSent()
{
  trace << "TCPSocket: all data has been ack'ed" << endl;
  myWriteSemaphore->signal(); // The data has been acknowledged
}

void
TCPSocket::socketEof()
{
  eofFound = true;
  myReadSemaphore->signal();
}

bool
TCPSocket::isEof() {
  return eofFound;
  // True if a FIN has been received from the remote host.
}


/* Closes the application, replaces app close in netclose.
 Called in doit method after while loop is done (while loop stops when typing
 q)
*/
void
TCPSocket::Close(){
	myConnection->AppClose();
}


SimpleApplication::SimpleApplication(TCPSocket* theSocket):
mySocket(theSocket){

}

void
SimpleApplication::doit() // First part until q just copied from lab manual.
{
  trace << "SimpleApplication::doit" << endl;
  udword aLength;
  byte* aData;
  bool done = false;
  while (!done && !mySocket->isEof()){
    aData = mySocket->Read(aLength);
    if (aLength > 0)
    {
      mySocket->Write(aData, aLength);
      if ((char)*aData == 'q')
      {
        trace << "-------Closing application with q-------" << endl;
        done = true; // Close the for loop
      } else if((char)*aData == 's'){
        trace << "-------SimpleApplication::doit found s in stream-------" << endl;
        byte* space =  (byte*) malloc(1000*1000);
        for (int i = 0; i < 1000*1000; i++)
        {
          space[i] = 'A' + i % (0x5B-0x41); // Creates random letter.
        }
        mySocket->Write(space, 1000*1000);
        delete space;

      } else if((char)*aData == 'r') {
        trace << "-------SimpleApplication::doit found r in stream-------" << endl;
        byte* space =  (byte*) malloc(10000);
        for (int i = 0; i < 10000; i++)
        {
          space[i] = 'Z' - i % (0x5B-0x41);
        }
        mySocket->Write(space, 10000);
        delete space;
      } else if((char)*aData == 't') {
        trace << "-------SimpleApplication::doit found t in stream-------" << endl;
        byte* space =  (byte*) malloc(360);
        for (int i = 0; i < 360; i++)
        {
          space[i] = '0' + i % 10;
        }
        mySocket->Write(space, 360);
        delete space;
      }

      delete aData;
    }
  }
  mySocket->Close(); // Close application after while loop ended.
}
