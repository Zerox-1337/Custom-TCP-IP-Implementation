/*!***************************************************************************
*!
*! FILE NAME  : tcp.cc
*!
*! DESCRIPTION: TCP, Transport control protocol
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
#include "http.hh"

//#define D_TCP
#ifdef D_TCP
#define trace cout
#else
#define trace if(false) cout
#endif
/****************** TCP DEFINITION SECTION *************************/

//----------------------------------------------------------------------------
//
TCP::TCP()
{
  trace << "TCP created." << endl;
}

//----------------------------------------------------------------------------
//
TCP&
TCP::instance()
{
  static TCP myInstance;
  return myInstance;
}

//----------------------------------------------------------------------------
//
TCPConnection*
TCP::getConnection(IPAddress& theSourceAddress,
                   uword      theSourcePort,
                   uword      theDestinationPort)
{
  TCPConnection* aConnection = NULL;
  // Find among open connections
  uword queueLength = myConnectionList.Length();
  myConnectionList.ResetIterator();
  bool connectionFound = false;
  while ((queueLength-- > 0) && !connectionFound)
  {
    aConnection = myConnectionList.Next();
    connectionFound = aConnection->tryConnection(theSourceAddress,
                      theSourcePort,
                      theDestinationPort);
  }
  if (!connectionFound)
  {
    trace << "Connection not found!" << endl;
    aConnection = NULL;
  }
  else
  {
    trace << "Found connection in queue" << endl;
  }
  return aConnection;
}

//----------------------------------------------------------------------------
//
TCPConnection*
TCP::createConnection(IPAddress& theSourceAddress,
                      uword      theSourcePort,
                      uword      theDestinationPort,
                      InPacket*  theCreator)
{
  TCPConnection* aConnection =  new TCPConnection(theSourceAddress,
      theSourcePort,
      theDestinationPort,
      theCreator);
  myConnectionList.Append(aConnection);
  return aConnection;
}


bool
TCP::acceptConnection(uword port) {
  if (port == 7) {
    return true;
  } else if (port == 80) {
    return true;
  }
  return false;
}

void
TCP::connectionEstablished(TCPConnection *theConnection)
{
  trace << "TCP::connectionEstablished" << endl;
  if (theConnection->serverPortNumber() == 7)
  {
    trace << "found port 7" << endl;
    TCPSocket* aSocket = new TCPSocket(theConnection);
    // Create a new TCPSocket.
    theConnection->registerSocket(aSocket);
    // Register the socket in the TCPConnection.
    Job::schedule(new SimpleApplication(aSocket));
    // Create and start an application for the connection.
  } else if (theConnection->serverPortNumber() == 80) {
    trace << "found port 80" << endl;
    TCPSocket* aSocket = new TCPSocket(theConnection);
    // Create a new TCPSocket.
    theConnection->registerSocket(aSocket);
    // Register the socket in the TCPConnection.
    Job::schedule(new HTTPServer(aSocket));
  }
}

//----------------------------------------------------------------------------
//
void
TCP::deleteConnection(TCPConnection* theConnection)
{
  myConnectionList.Remove(theConnection);

  delete theConnection;
}

//----------------------------------------------------------------------------
//
TCPConnection::TCPConnection(IPAddress& theSourceAddress,
                             uword      theSourcePort,
                             uword      theDestinationPort,
                             InPacket*  theCreator):
  hisAddress(theSourceAddress),
  hisPort(theSourcePort),
  myPort(theDestinationPort),
  windowSizeSemaphore(Semaphore::createQueueSemaphore("Window Size", 0))
{
  trace << "TCP connection created" << endl;
  sentMaxSeq = 0;
  RSTFlag = false;
  myTCPSender = new TCPSender(this, theCreator),
  myState = ListenState::instance();
  myTimer = new retransmitTimer(this, Clock::tics * 200);
  activeSocket = false;
  finSent = false;
}

//----------------------------------------------------------------------------
//
TCPConnection::~TCPConnection()
{
  trace << "TCP connection destroyed" << endl;
  if (activeSocket) {
    delete mySocket;
  }
  delete myTCPSender;
  delete windowSizeSemaphore;
  delete myTimer;
  //cout << "after delete mySocket" << endl;
}

void
TCPConnection::RSTFlagReceived(){
  RSTFlag = true;
  myTimer->cancel();
  if(finSent) {
    return;
  }
  mySocket->socketDataSent();
}

//----------------------------------------------------------------------------
//
bool
TCPConnection::tryConnection(IPAddress& theSourceAddress,
                             uword      theSourcePort,
                             uword      theDestinationPort)
{
  return (theSourcePort      == hisPort   ) &&
         (theDestinationPort == myPort    ) &&
         (theSourceAddress   == hisAddress);
}


// TCPConnection cont...
void
TCPConnection::Synchronize(udword theSynchronizationNumber) {
  myState->Synchronize(this, theSynchronizationNumber);
  // Handle an incoming SYN segment

  //TODO
}
void
TCPConnection::NetClose() {
  myState->NetClose(this);
  // Handle an incoming FIN segment
  //DONE
}
void
TCPConnection::AppClose() {
  myState->AppClose(this);
}

void
TCPConnection::Kill() {
  // Handle an incoming RST segment, can also called in other error conditions
  //Send fin?
  myState->Kill(this);
  //TODO
}
void
TCPConnection::Receive(udword theSynchronizationNumber, byte*  theData, udword theLength) {
  // Handle incoming data
  myState->Receive(this, theSynchronizationNumber, theData, theLength);
  //TODO
}
void
TCPConnection::Acknowledge(udword theAcknowledgementNumber) {
  // Handle incoming Acknowledgemen
  if (sentMaxSeq == theAcknowledgementNumber) {
    myTimer->cancel();
    // cout << "canceling timer" << endl;

  }
  myState->Acknowledge(this, theAcknowledgementNumber);
  //TODO
}
void
TCPConnection::Send(byte* theData, udword theLength) {
  // Send outgoing data
  //TODO
  //myState->Send(this, theData, theLength);
  myState->Send(this, theData, theLength);
}

uword
TCPConnection::serverPortNumber() {
  return myPort;
}

void
TCPConnection::registerSocket(TCPSocket* theSocket) {
  activeSocket = true;
  mySocket = theSocket;
}

udword
TCPConnection::theOffset() {
  return sendNext - firstSeq;
}

byte*
TCPConnection::theFirst() {
  return transmitQueue + theOffset();
}

udword
TCPConnection::theSendLength() {
  trace << "the offset: " << theOffset() << " queueLength: " << queueLength << endl;
  if ((queueLength - theOffset()) < 1396) {
    return queueLength - theOffset();
  } else {
    return 1396;
  }
}

//----------------------------------------------------------------------------
// TCPState contains dummies for all the operations, only the interesting ones
// gets overloaded by the various sub classes.


//----------------------------------------------------------------------------
//
void
TCPState::Kill(TCPConnection* theConnection)
{
  // Handle an incoming RST segment, can also called in other error conditions

  //cout << "TCPState::Kill" << endl;
  TCP::instance().deleteConnection(theConnection);
}

void
TCPState::Synchronize(TCPConnection* theConnection, udword theSynchronizationNumber)
{
  cout << "TCPState::Synchronize port: " << theConnection->hisPort <<  endl;
  // Handle an incoming SYN segment
  //TODO
}

void
TCPState::NetClose(TCPConnection* theConnection)
{
  // Handle an incoming FIN segment
  //TODO
  cout << "TCPState::NetClose port: " << theConnection->hisPort << endl;
}

void
TCPState::AppClose(TCPConnection* theConnection)
{
  // Handle close from application
  //TODO
  cout << "TCPState::AppClose port: " << theConnection->hisPort << endl;
}

void
TCPState::Receive(TCPConnection* theConnection, udword theSynchronizationNumber, byte*  theData, udword theLengthn)
{
  // Handle incoming data
  //TODO
  cout << "TCPState::Receive port: " << theConnection->hisPort << endl;
}

void
TCPState::Acknowledge(TCPConnection* theConnection, udword theAcknowledgementNumber)
{
  // Handle incoming Acknowledgement
  //TODO

  trace << "TCPState::Acknowledge port: " << theConnection->hisPort << endl;
}

void
TCPState::Send(TCPConnection* theConnection, byte* theData, udword theLength)
{
  // Send outgoing data
  //TODO
  cout << "TCPState::Send port: " << theConnection->hisPort << endl;
}


//----------------------------------------------------------------------------
//
ListenState*
ListenState::instance()
{
  static ListenState myInstance;
  return &myInstance;
}


void
ListenState::Synchronize(TCPConnection* theConnection, udword theSynchronizationNumber)
{
  if (TCP::instance().acceptConnection(theConnection->myPort)) {
    trace << "got SYN on ECHO port" << endl;
    theConnection->receiveNext = theSynchronizationNumber + 1;
    theConnection->receiveWindow = 8 * 1024;
    theConnection->sendNext = get_time();
    // Next reply to be sent.
    theConnection->sentUnAcked = theConnection->sendNext;
    // Send a segment with the SYN and ACK flags set.
    theConnection->myTCPSender->sendFlags(0x12);
    // Prepare for the next send operation.
    theConnection->sendNext += 1;
    // Change state
    theConnection->myState = SynRecvdState::instance();
    trace << "SynRecvdState state set" << endl;
  } else {
    cout << "send RST..." << endl;
    theConnection->sendNext = 0;
    // Send a segment with the RST flag set.
    theConnection->myTCPSender->sendFlags(0x04);
    TCP::instance().deleteConnection(theConnection);
  }

}

//----------------------------------------------------------------------------
//
SynRecvdState*
SynRecvdState::instance()
{
  static SynRecvdState myInstance;
  return &myInstance;
}

void
SynRecvdState::Acknowledge(TCPConnection* theConnection, udword theAcknowledgementNumber) {
//DONE
  trace << "SynRecvdState::Acknowledge" << endl;
  if (theAcknowledgementNumber == theConnection->sendNext) {
    trace << "EstablishedState::instance()" << endl;
    theConnection->myState = EstablishedState::instance();
    TCP::instance().connectionEstablished(theConnection);
  } else {
    trace << "Wrong ackNbr" << endl;
    theConnection->Kill();
  }
  /*
  if (theAcknowledgementNumber > theConnection->sentUnAcked) {
  trace << "ACK received, changing to EstablishedState" << endl;
  // Setting the last acked segment
  theConnection->sentUnAcked = theAcknowledgementNumber;

  // Changing state to established
  theConnection->myState = EstablishedState::instance();
  */
// }

}



//----------------------------------------------------------------------------
//
EstablishedState*
EstablishedState::instance()
{
  static EstablishedState myInstance;
  return &myInstance;
}


//----------------------------------------------------------------------------
//
void
EstablishedState::NetClose(TCPConnection* theConnection)
{
  trace << "EstablishedState::NetClose" << endl;

  // Update connection variables and send an ACK
  //DONE
  (theConnection->receiveNext) += 1;
  theConnection->myTCPSender->sendFlags(0x10);

  theConnection->mySocket->socketEof();

  // Normally the application would be notified next and nothing
  // happen until the application calls appClose on the connection.
  // Since we don't have an application we simply call appClose here instead.

}

//----------------------------------------------------------------------------
//
void
EstablishedState::Receive(TCPConnection* theConnection,
                          udword theSynchronizationNumber,
                          byte*  theData,
                          udword theLength)
{

  trace << "EstablishedState::Receive" << endl;

  //cout << "theSynchronizationNumber: " << theSynchronizationNumber << " receiveNext: " << theConnection->receiveNext<<endl;

  if (theSynchronizationNumber == theConnection->receiveNext && theLength != 0) {
    //cout << "EstablishedState::Receive syncNbr == recNext" << endl;

    theConnection->receiveNext += theLength;
    //theConnection->sentUnAcked = theConnection->sendNext;
    theConnection->myTCPSender->sendFlags(0x10); //ACK


    theConnection->mySocket->socketDataReceived(theData, theLength);
  } else {
    //cout << "-----deleting data----" << endl;
    delete [] theData; 
  }

  // Delayed ACK is not implemented, simply acknowledge the data
  // by sending an ACK segment, then echo the data using Send.

}

void
EstablishedState::Acknowledge(TCPConnection* theConnection, udword theAcknowledgementNumber) {
  //cout << "EstablishedState::Acknowledge" << endl;
  //cout << "Ack port: " << theConnection->hisPort << " Number: " << theAcknowledgementNumber << endl;

  if (theAcknowledgementNumber > theConnection->sendNext) {
    theConnection->sendNext = theAcknowledgementNumber;
  }

  if (theAcknowledgementNumber > theConnection->sentUnAcked) {
    trace << "rec ack greater than unAcked" << endl;
    // Setting the last acked segment
    theConnection->sentUnAcked = theAcknowledgementNumber;
  }
  //cout << "theAcknowledgementNumber: " << theAcknowledgementNumber <<
  //"sendNext: " << theConnection->sendNext << endl;
  trace << "before windowsizesema signal" << endl;
  theConnection->windowSizeSemaphore->signal();
  trace << "after windowsizesema signal" << endl;
  if (theConnection->sentMaxSeq == theAcknowledgementNumber) {
    theConnection->mySocket->socketDataSent();
  }
}

void
EstablishedState::Send(TCPConnection* theConnection, byte*  theData, udword theLength) {
  //TODO
  theConnection->transmitQueue = theData;
  theConnection->queueLength = theLength;
  theConnection->firstSeq = theConnection->sendNext;
  while (theConnection->theOffset() != theConnection->queueLength) {
    if(theConnection->RSTFlag == false){
      theConnection->myTCPSender->sendFromQueue();
    } else {
      theConnection->myTimer->cancel();
      return;
    }
  }

}

void
EstablishedState::AppClose(TCPConnection* theConnection) {
  trace << "EstablishedState::AppClose" << endl;
  if (theConnection->RSTFlag) {
    //cout << "-----------------RSTFlag promted close from established state-----" << endl;
    
    theConnection->Kill();
    return;
  }

  if(theConnection->mySocket->isEof()){
    theConnection->myState = CloseWaitState::instance();
    theConnection->AppClose();
  } else {
    theConnection->myState = FinWait1State::instance();
    theConnection->myTCPSender->sendFlags(0x11); //Send FIN
    theConnection->finSent = true;
    theConnection->sendNext = theConnection->sendNext + 1;
  }
  
}


//----------------------------------------------------------------------------
//
CloseWaitState*
CloseWaitState::instance()
{
  static CloseWaitState myInstance;
  return &myInstance;
}

void
CloseWaitState::AppClose(TCPConnection* theConnection) {
  //DONE
  trace << "CloseWaitState::AppClose" << endl;
  theConnection->myTCPSender->sendFlags(0x11);
  theConnection->myState = LastAckState::instance();

  //theConnection->Kill(); // should not be done here, ACK should come from linus first
}

FinWait1State*
FinWait1State::instance()
{
  static FinWait1State  myInstance;
  return &myInstance;
}

void
FinWait1State::Acknowledge(TCPConnection* theConnection, udword theAcknowledgementNumber) {
  //DONE
  trace << "FinWait1State ack" << endl;
  //cout << theConnection->hisPort << " <------the port, theConnection->sendNext in FinWait1State::sendNext is: " << theConnection->sendNext << " AckNbr: " << theAcknowledgementNumber<< endl;
  if (theConnection->sendNext == theAcknowledgementNumber) {
    //cout << "Correct ack number" << endl;
    theConnection->myState = FinWait2State::instance();
  } else {
    //cout << theConnection->hisPort << " <------the port, Incorrect--theConnection->sendNext in FinWait1State::sendNext is: " << theConnection->sendNext << " AckNbr: " << theAcknowledgementNumber<< endl;
    theConnection->Kill();
    //cout << "---------------Incorrect ack number---------------" << endl;
  }
}


FinWait2State*
FinWait2State::instance()
{
  static FinWait2State   myInstance;
  return &myInstance;
}

void
FinWait2State::NetClose(TCPConnection* theConnection) {
  //cout << theConnection->hisPort << " FinWait2State::NetClose" << endl;
  theConnection->receiveNext += 1;
  theConnection->myTCPSender->sendFlags(0x10);
  theConnection->Kill();
  /*
  cout << "connections open: " << TCP::instance().myConnectionList.Length() << endl;
  for (int i = 0; i < TCP::instance().myConnectionList.Length(); i++) {
    cout << "port: " << TCP::instance().myConnectionList.Next()->hisPort << endl;
  }
  */
  //theConnection->myState TimeWait::instance(); //Perhaps add with timeout
}
/*
TimeWait*
TimeWait::instance()
{
  static TimeWait   myInstance;
  return &myInstance;
}
*/
//----------------------------------------------------------------------------
//
LastAckState*
LastAckState::instance()
{
  static LastAckState myInstance;
  return &myInstance;
}
void
LastAckState::Acknowledge(TCPConnection* theConnection, udword theAcknowledgementNumber) {
  //DONE
  //cout << "LastAckState::Acknowledge" << endl;
  if (theConnection->receiveNext == theAcknowledgementNumber) {
    trace << "Correct LastAck ack number" << endl;
    theConnection->Kill();
  } else {
    trace << "INcorrect LastAck ack number, terminiating anyway" << endl;
    theConnection->Kill();
  }
}

//----------------------------------------------------------------------------
//
TCPSender::TCPSender(TCPConnection* theConnection,
                     InPacket*      theCreator):
  myConnection(theConnection),
  myAnswerChain(theCreator->copyAnswerChain()) // Copies InPacket chain!
{
}

//----------------------------------------------------------------------------
//
TCPSender::~TCPSender()
{
  myAnswerChain->deleteAnswerChain();
}
void
TCPSender::sendFlags(byte theFlags)
{
  trace << "TCPSender::sendFlags" << endl;
  // Decide on the value of the length totalSegmentLength.
  // Allocate a TCP segment.
  uword hoffs = myAnswerChain->headerOffset();
  uword totalSegmentLength = TCP::tcpHeaderLength; //data = 0

  byte* anAnswer = new byte[hoffs + totalSegmentLength];
  anAnswer += hoffs;
  // Calculate the pseudo header checksum
  TCPPseudoHeader* aPseudoHeader =
    new TCPPseudoHeader(myConnection->hisAddress,
                        totalSegmentLength);
  uword pseudosum = aPseudoHeader->checksum();
  delete aPseudoHeader;
  // Create the TCP segment.
  TCPHeader* aTCPHeader = (TCPHeader*) anAnswer;
  aTCPHeader->sourcePort = HILO(myConnection->myPort);

  aTCPHeader->destinationPort = HILO(myConnection->hisPort);
  aTCPHeader->sequenceNumber = LHILO(myConnection->sendNext);
  aTCPHeader->acknowledgementNumber = LHILO(myConnection->receiveNext);
  //byte headerShifted = (TCP::tcpHeaderLength/4) << 4;  //32 bit words => (/4)

  //uword temp = aTCPHeader->headerLength;
 
  aTCPHeader->headerLength = (byte) (TCP::tcpHeaderLength << 2);
  
  //aTCPHeader->headerLength = headerShifted;
  //aTCPHeader->headerLength |= temp; // so we don't overwrite flags/reserved bits
  aTCPHeader->flags = theFlags;
  aTCPHeader->windowSize = HILO(myConnection->receiveWindow);
  aTCPHeader->urgentPointer = 0;
  aTCPHeader->checksum = 0;


  // Calculate the final checksum.
  aTCPHeader->checksum = calculateChecksum(anAnswer,
                         totalSegmentLength,
                         pseudosum);
  // Send the TCP segment.
  trace << "SENDING Tcp Packet! Src port: " << myConnection->myPort << " Dest port: " <<
        myConnection->hisPort << " Seq#: " << myConnection->sendNext << " Ack#: " << myConnection->receiveNext << endl;
  myAnswerChain->answer(anAnswer, totalSegmentLength);
  // Deallocate the dynamic memory
}

void
TCPSender::sendData(byte* theData, udword theLength) {
  //TODO




  trace << "TCPSender::sendData" << endl;
  // Decide on the value of the length totalSegmentLength.
  // Allocate a TCP segment.
  uword hoffs = myAnswerChain->headerOffset();
  uword totalSegmentLength = theLength + TCP::tcpHeaderLength; //data = 0
  byte* anAnswer = new byte[hoffs + totalSegmentLength];
  anAnswer += hoffs;
  memcpy(anAnswer + TCP::tcpHeaderLength, theData, theLength);
  // Calculate the pseudo header checksum
  TCPPseudoHeader* aPseudoHeader =
    new TCPPseudoHeader(myConnection->hisAddress,
                        totalSegmentLength);
  uword pseudosum = aPseudoHeader->checksum();
  delete aPseudoHeader;
  // Create the TCP segment.
  TCPHeader* aTCPHeader = (TCPHeader*) anAnswer;
  aTCPHeader->sourcePort = HILO(myConnection->myPort);
  aTCPHeader->destinationPort = HILO(myConnection->hisPort);
  aTCPHeader->sequenceNumber = LHILO(myConnection->sendNext);
  aTCPHeader->acknowledgementNumber = LHILO(myConnection->receiveNext);
  //byte headerShifted = (TCP::tcpHeaderLength/4) << 4;  //32 bit words => (/4)

  //uword temp = aTCPHeader->headerLength;
  aTCPHeader->headerLength = (byte) (TCP::tcpHeaderLength << 2);
  //aTCPHeader->headerLength = headerShifted;
  //aTCPHeader->headerLength |= temp; // so we don't overwrite flags/reserved bits
  aTCPHeader->flags = 0x18;
  aTCPHeader->windowSize = HILO(myConnection->receiveWindow);
  aTCPHeader->urgentPointer = 0;
  aTCPHeader->checksum = 0;

  // Calculate the final checksum.
  aTCPHeader->checksum = calculateChecksum(anAnswer,
                         totalSegmentLength,
                         pseudosum);
  // Send the TCP segment.
  trace << "SENDING Tcp Packet DATA! Src port: " << myConnection->myPort << " Dest port: " <<
        myConnection->hisPort << " Seq#: " << myConnection->sendNext << " Ack#: " << myConnection->receiveNext << endl;
  myConnection->sendNext += theLength; //Increase seq
  if (myConnection->sentMaxSeq < myConnection->sendNext) {
    myConnection->sentMaxSeq = myConnection->sendNext;
  }

  throwIndex++;
  throwIndex = throwIndex % 100;
  //if (throwIndex == 0) {
  if(false) {
    //cout << "-------------Throwing away packet before transmit ---------------" << endl;
    anAnswer -= hoffs;
    delete anAnswer;
  } else {
    myAnswerChain->answer(anAnswer, totalSegmentLength);
  }
  // Deallocate the dynamic memory
}


void
TCPSender::sendFromQueue() {
  udword theWindowSize = myConnection->myWindowSize - (myConnection->sendNext - myConnection->sentUnAcked);
  if (theWindowSize > myConnection->myWindowSize) {
    theWindowSize = 0;
  }
  udword min = MIN(theWindowSize, myConnection->theSendLength());
  //cout << " IN QUEUE: sendNext: " << myConnection->sendNext << " sentMaxSeq " << myConnection->sentMaxSeq << endl;
  //cout << "available size on reciever end is: " << theWindowSize << endl;
  if (myConnection->sendNext < myConnection->sentMaxSeq) { //retransmit
    sendData(myConnection->theFirst(), myConnection->theSendLength());

  } else {
    while (min <= 0) {
      //cout << "sedfromqueue while enter" << endl;
      myConnection->windowSizeSemaphore->wait();
      theWindowSize = myConnection->myWindowSize - (myConnection->sendNext - myConnection->sentUnAcked);
      if (theWindowSize > myConnection->myWindowSize) {
        theWindowSize = 0;
      }
      min = MIN(theWindowSize, myConnection->theSendLength());
      //cout << "min size was 0, got signal" << endl;
    }
    /* cout <<
     "min value: " << min <<
     " sent, not acked: " << (myConnection->sendNext - myConnection->sentUnAcked) <<
       " myWindowSize: " << myConnection->myWindowSize  << endl;*/
    sendData(myConnection->theFirst(), min);
  }
  myConnection->myTimer->start();

}
//----------------------------------------------------------------------------
//
TCPInPacket::TCPInPacket(byte*           theData,
                         udword          theLength,
                         InPacket*       theFrame,
                         IPAddress&      theSourceAddress):
  InPacket(theData, theLength, theFrame),
  mySourceAddress(theSourceAddress)
{
}


//----------------------------------------------------------------------------
//
InPacket*
TCPInPacket::copyAnswerChain()
{
  return myFrame->copyAnswerChain();
}

void
TCPInPacket::decode()
{
  // Extract the parameters from the TCP header which define the
  // connection.
  TCPHeader* aTCPHeader = (TCPHeader*) myData;
  myDestinationPort = HILO(aTCPHeader->destinationPort);
  mySourcePort = HILO(aTCPHeader->sourcePort);
  //cout << "DECODE b4 set---- SeqNbr: " << mySequenceNumber << endl;
  mySequenceNumber = LHILO(aTCPHeader->sequenceNumber);
  //cout << "DECODE after set---- SeqNbr: " << mySequenceNumber << endl;
  myAcknowledgementNumber = LHILO(aTCPHeader->acknowledgementNumber);

 // cout << "Incoming TCP packet! Src port: " << mySourcePort << " Dest port: " <<
  //      myDestinationPort << " Seq#: " << mySequenceNumber << " Ack#: " << myAcknowledgementNumber;
  //cout << "before getConn port: " << mySourcePort << endl;
  TCPConnection* aConnection =
    TCP::instance().getConnection(mySourceAddress,
                                  mySourcePort,
                                  myDestinationPort);
  //cout << "After getConnection" << endl;


  if (!aConnection)
  {
    //cout << "connections open: " << TCP::instance().myConnectionList.Length() << endl;
    /*
    for (int i = 0; i < TCP::instance().myConnectionList.Length(); i++) {
      cout << "port: " << TCP::instance().myConnectionList.Next()->hisPort << endl;
    }
    */
    
    //os_delay(10);
    if ((aTCPHeader->flags & 0x04) == 0x04) {
      return; //no connection is establisehd, RST DOS.
    }
    //cout << "Connnection not found on port: " << mySourcePort << endl;
    // Establish a new connection.
    //if connections > antal quit this scrap
    if (TCP::instance().myConnectionList.Length() > 20) {
      delete myData;
     // cout << "denied connection, already 20 open" << endl;
      return;
    }
    aConnection =
      TCP::instance().createConnection(mySourceAddress,
                                       mySourcePort,
                                       myDestinationPort,
                                       this);
    if ((aTCPHeader->flags & 0x02) != 0)
    {
      // State LISTEN. Received a SYN flag.
      aConnection->Synchronize(mySequenceNumber);
    }
    else
    {
      // State LISTEN. No SYN flag. Impossible to continue.
      trace << "no SYN flag in new conn port: " << aConnection->hisPort << endl;
      aConnection->Kill();
    }
  }
  else
  {
    if ((aTCPHeader->flags & 0x04) == 0x04) { //RST flag
      //cout << " RST FLAG port: "<< aConnection->hisPort << endl;
      //aConnection->Kill();
      aConnection->RSTFlagReceived();
      //aConnection->Acknowledge(myAcknowledgementNumber);
      return;

      //cout << " successfully killed after rst flag" << endl;
    }
    //DONE
    trace << "Decoding incoming flags in TCP layer." << endl;
    aConnection->myWindowSize = HILO(aTCPHeader->windowSize);
    if ((aTCPHeader->flags & 0x18) == 0x18) { //ACK and PSH flag
      //cout << " found ACK and PSH flag" << endl;
      aConnection->Receive(mySequenceNumber, myData + TCP::tcpHeaderLength, myLength - TCP::tcpHeaderLength);
      //cout << "After rec" << endl;
    } else if ((aTCPHeader->flags & 0x11) == 0x11) { //ACK and FIN flag
      //cout << " found ACK and FIN flag" << endl;
      aConnection->Acknowledge(myAcknowledgementNumber);
      aConnection->NetClose();
    } else if ((aTCPHeader->flags & 0x10) == 0x10) { //ACK flag
      //cout << " found ACK flag" << endl;

      //Data is sent in Ack, if we already received this ack. (Duplicate Ack)
      if(myAcknowledgementNumber == aConnection->sentUnAcked){
        //cout << "Receiveing data in ACK on port: " << aConnection->hisPort << " size: " << myLength - TCP::tcpHeaderLength << endl;
        aConnection->Receive(mySequenceNumber, myData + TCP::tcpHeaderLength, myLength - TCP::tcpHeaderLength);
      } else {
        aConnection->Acknowledge(myAcknowledgementNumber);
      }
    }
   
    if ((aTCPHeader->flags & 0x02) == 0x02) {// SYN flag
      //cout << " Received syn flag, should not?" << endl;
      //aConnection->Synchronize(mySequenceNumber);
    }
    //cout << "Done decoding port: " << mySourcePort << endl;
  }
}

void
TCPInPacket::answer(byte* theData, udword theLength) {
  //TODO
  trace << "TCPInPacket::anwer" << endl;
}

uword
TCPInPacket::headerOffset() {
  return myFrame->headerOffset() + TCP::tcpHeaderLength; //DONE?
}

//----------------------------------------------------------------------------
//
TCPPseudoHeader::TCPPseudoHeader(IPAddress& theDestination,
                                 uword theLength):
  sourceIPAddress(IP::instance().myAddress()),
  destinationIPAddress(theDestination),
  zero(0),
  protocol(6)
{
  tcpLength = HILO(theLength);
}

//----------------------------------------------------------------------------
//
uword
TCPPseudoHeader::checksum()
{
  return calculateChecksum((byte*)this, 12);
}

retransmitTimer::retransmitTimer(TCPConnection* theConnection, Duration retransmitTime):
  myConnection(theConnection),
  myRetransmitTime(retransmitTime)
{

}

void
retransmitTimer::start() {
  //cout << "timer started" << endl;
  this->timeOutAfter(myRetransmitTime);
}

void
retransmitTimer::cancel() {
  //cout << "timer canceled" << endl;
  this->resetTimeOut();
}
void
retransmitTimer::timeOut() {
  cout << "timer timed out! " << "Port: " << myConnection->hisPort <<  endl;
  myConnection->sendNext = myConnection->sentUnAcked;
  myConnection->myTCPSender->sendFromQueue();

}


/****************** END OF FILE tcp.cc *************************************/
