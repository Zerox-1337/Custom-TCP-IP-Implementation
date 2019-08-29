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

#define D_TCP
#ifdef D_TCP

#else

#endif

#define trace if(false) cout
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

//----------------------------------------------------------------------------
//

bool
TCP::acceptConnection(uword portNo){

    return portNo == 7 || portNo == 80; // Lab 6 added
}
// Is true when a connection is accepted on port portNo.

void
TCP::connectionEstablished(TCPConnection *theConnection) { //Copied from lab manual
    if (theConnection->serverPortNumber() == 7) {
        trace << "found port 7" << endl;
        TCPSocket* aSocket = new TCPSocket(theConnection);
        // Create a new TCPSocket.
        theConnection->registerSocket(aSocket);
        // Register the socket in the TCPConnection.
        Job::schedule(new SimpleApplication(aSocket));
        // Create and start an application for the connection.
    }
    else if (theConnection->serverPortNumber() == 80) // lab 6 added
    {
      trace << "found port 80" << endl;
      TCPSocket* aSocket = new TCPSocket(theConnection);
      // Create a new TCPSocket.
      theConnection->registerSocket(aSocket);
      // Register the socket in the TCPConnection.
      Job::schedule(new HTTPServer(aSocket));
    }
}
// Create a new TCPSocket. Register it in TCPConnection.
// Create and start a SimpleApplication.

void
TCP::deleteConnection(TCPConnection* theConnection){
    myConnectionList.Remove(theConnection);
    delete theConnection;
}

//----------------------------------------------------------------------------
//



//----------------------------------------------------------------------------
//


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
  myTCPSender = new TCPSender(this, theCreator), // The inpacket.
  myState = ListenState::instance();
  myTimer = new retransmitTimer(this, Clock::tics*200);

  // Lab 6 Added

  activeSocket = false;
  finSent = false;

}



//----------------------------------------------------------------------------
//
TCPConnection::~TCPConnection()
{
  trace << "TCP connection destroyed" << endl;
  if (activeSocket == true) // If we have an active socket. Added lab 6
  {
    delete mySocket;
  }
  delete myTCPSender;
  delete windowSizeSemaphore;
  delete myTimer;
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

//----------------------------------------------------------------------------
//
void
TCPConnection::Synchronize(udword theSynchronizationNumber)
{
  myState->Synchronize(this, theSynchronizationNumber);
}
//----------------------------------------------------------------------------
//
void
TCPConnection::NetClose(){
  myState->NetClose(this);
}
// Handle an incoming FIN segment
//----------------------------------------------------------------------------
//
void
TCPConnection::AppClose(){
  myState->AppClose(this);
}
// Handle close from application
//----------------------------------------------------------------------------
//
void
TCPConnection::Kill(){
  myState->Kill(this);
}
// Handle an incoming RST segment, can also called in other error conditions
//----------------------------------------------------------------------------
//
void
TCPConnection::Receive(udword theSynchronizationNumber,
             byte*  theData,
             udword theLength){
  myState->Receive(this, theSynchronizationNumber, theData, theLength);
}
// Handle incoming data
//----------------------------------------------------------------------------
//
void
TCPConnection::Acknowledge(udword theAcknowledgementNumber){
  if (sentMaxSeq == theAcknowledgementNumber) // We have ack the highest sequence number sent so far.
  {
    myTimer->cancel();
  }

    myState->Acknowledge(this, theAcknowledgementNumber); // calls acknowledge for the state we are in.
}
// Handle incoming Acknowledgement
//----------------------------------------------------------------------------
//
void
TCPConnection::Send(byte*  theData,udword theLength){
    myState->Send(this, theData, theLength);

}
// Send outgoing data
//----------------------------------------------------------------------------
//
uword
TCPConnection::serverPortNumber(){
    return myPort;
}
// Return myPort.
//----------------------------------------------------------------------------
//
void
TCPConnection::registerSocket(TCPSocket* theSocket){
    activeSocket = true;
    mySocket = theSocket;
}
// Set mySocket to theSocket.


udword
TCPConnection::theOffset() { // the first position in the que relative transmitQueue
  trace << "sendNext: " << sendNext << " firstSeq: " << firstSeq << endl;
  return sendNext - firstSeq;
}

byte*
TCPConnection::theFirst() { // The first byte to send in the segment relative to transmittQue
  return transmitQueue + theOffset();
}

udword
TCPConnection::theSendLength() { // The number of bytes to send in a single segment
  trace << "the offset: " << theOffset() << " queueLength: " << queueLength << endl;
  if ((queueLength - theOffset()) < 1396) { // The number of data to be sent - First position in queque (relative transmitqueue)
    return queueLength - theOffset();
  } else {
    return 1396; // Why 1396?
  }
}

//----------------------------------------------------------------------------
// TCPState contains dummies for all the operations, only the interesting ones
// gets overloaded by the various sub classes.


//----------------------------------------------------------------------------
//
void
TCPState::Synchronize(TCPConnection* theConnection,
                         udword theSynchronizationNumber){

}
// Handle an incoming SYN segment
void
TCPState::NetClose(TCPConnection* theConnection){

}
// Handle an incoming FIN segment
void
TCPState::AppClose(TCPConnection* theConnection){

}
// Handle close from application

void
TCPState::Kill(TCPConnection* theConnection)
{
  trace << "TCPState::Kill" << endl;
  TCP::instance().deleteConnection(theConnection);
}

void
TCPState::Receive(TCPConnection* theConnection,
                     udword theSynchronizationNumber,
                     byte*  theData,
                     udword theLength){

}
// Handle incoming data
void
TCPState::Acknowledge(TCPConnection* theConnection,
                         udword theAcknowledgementNumber){

}
// Handle incoming Acknowledgement
void
TCPState::Send(TCPConnection* theConnection,
                  byte*  theData,
                  udword theLength){

}

  // Send outgoing data



//----------------------------------------------------------------------------
//
ListenState*
ListenState::instance()
{
  static ListenState myInstance;
  return &myInstance;
}

void
ListenState::Synchronize(TCPConnection* theConnection,
                         udword theSynchronizationNumber) // called in TCPinpacket::Decode
{
  // Sequence number: Client uses sequence number to keep track of how much data it has sent
  // acknowledge number: Server to inform the sending host that the transmitted data was received successfully
  //trace << (dec) << (int) theConnection->myPort << endl;

     if (TCP::instance().acceptConnection(theConnection->myPort)) // returns true if port 7.
     {
         trace << "got SYN on ECHO port" << endl;
         theConnection->receiveNext = theSynchronizationNumber + 1; // The next expected sequence number from the other host
         theConnection->receiveWindow = 8*1024;
         theConnection->sendNext = get_time();
         // Next reply to be sent.
         theConnection->sentUnAcked = theConnection->sendNext;
         // Send a segment with the SYN and ACK flags set.
         // myTCPSender = class that Includes the packet (Creator) and the connection.
         theConnection->myTCPSender->sendFlags(0x12); // TCPConnection::SendFlags called. 12= syn, ack.
         // Prepare for the next send operation.
         theConnection->sendNext += 1; // the next sequence number to send is sendNext
         // Change state
         theConnection->myState = SynRecvdState::instance(); // Change state to SynRecvdState, to acknowledge the syn
     }
     else
     {

       trace << "send RST..." << endl;
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
SynRecvdState::Acknowledge(TCPConnection* theConnection, udword theAcknowledgementNumber){


  if (theAcknowledgementNumber == theConnection->sendNext) // acknowledgenumber = next sequence number the receiver expects to receive
  {
    trace << "Acknowledge SynRecvdState" << endl;
    theConnection->myState = EstablishedState::instance();
    TCP::instance().connectionEstablished(theConnection);

  }
  else
  {
    trace << "Acknowledge SynRecvdState error wrong ack num, closing connection" << endl;
    theConnection->Kill(); // wrong connection
  }






}
// Handle incoming Acknowledgement

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

  // Sequence number: Client uses sequence number to keep track of how much data it has sent
  // acknowledge number: Server to inform the sending host that the transmitted data was received successfully
  // Update connection variables and send an ACK
  theConnection->receiveNext += 1; // next expected sequence number updated from host.
  // Go to NetClose wait state, inform application
  theConnection->myTCPSender->sendFlags(0x10); // According to picture from book we send ACK.

  // Lab 5: socketEof should replace closewait transition in previous lab 4.
  //theConnection->myState = CloseWaitState::instance();
  theConnection->mySocket->socketEof();

  // Normally the application would be notified next and nothing
  // happen until the application calls appClose on the connection.
  // Since we don't have an application we simply call appClose here instead.

  // Simulate application Close...

  /* Lab 5: Appclose is instead invoked in TCPSocket::Close called when you
    finish TCPSocket::Doit (which happens when you type q)

  */
  //theConnection->AppClose(); // Closewaitstate::Appclose;
}

//----------------------------------------------------------------------------
//
void
EstablishedState::Receive(TCPConnection* theConnection,udword theSynchronizationNumber, byte* theData, udword theLength)
{
    trace << "EstablishedState::Receive" << endl;
    if(theSynchronizationNumber == theConnection->receiveNext && theLength != 0){

      theConnection->receiveNext = theConnection->receiveNext + theLength; // Per bytes. Next expected sequence number to be received
      //theConnection->sentUnAcked = theConnection->sendNext;
      theConnection->myTCPSender->sendFlags(0x10);
      //theConnection->myState->Send(theConnection, theData, theLength);
      theConnection->mySocket->socketDataReceived(theData, theLength);
      // Delayed ACK is not implemented, simply acknowledge the data
      // by sending an ACK segment, then echo the data using Send.
    }
    else
    {
      delete [] theData;
    }
}

void
EstablishedState::Acknowledge(TCPConnection* theConnection, udword theAcknowledgementNumber){

  if (theAcknowledgementNumber > theConnection->sendNext) {
    theConnection->sendNext = theAcknowledgementNumber;
  }

  if (theAcknowledgementNumber > theConnection->sentUnAcked) {
    trace << "rec ack greater than unAcked" << endl;
    // Setting the last acked segment
    theConnection->sentUnAcked = theAcknowledgementNumber;

  }

  theConnection->windowSizeSemaphore->signal();
if (theConnection->sentMaxSeq == theAcknowledgementNumber) {
  theConnection->mySocket->socketDataSent(); // When reading/writing unblocks wait statement by sending signal in semaphore (see tcpsocket comments)
}


}
// Handle incoming Acknowledgement

void
EstablishedState::Send(TCPConnection* theConnection, byte*  theData, udword theLength){
    trace << "EstablishState::Send" << endl;
    theConnection->transmitQueue = theData;
    theConnection->queueLength = theLength;
    theConnection->firstSeq = theConnection->sendNext;
    // Maximum number of sequences is if you send 1 byte in each que = sendNext -Firsteq.
    while (theConnection->theOffset() != theConnection->queueLength) {
      if(theConnection->RSTFlag == false){
        theConnection->myTCPSender->sendFromQueue();
      } else {
        theConnection->myTimer->cancel();
        return;
      }
  }


    /* Lab 5: From lab 4 we replace sendData with sendFromQueue so we can
    transmitt huge amount of data and ack little pieces at a time.
    */
    //theConnection->myTCPSender->sendData(theData, theLength);
}
// Send outgoing data

// Called in doit method of TCPsocket, when you type q (leave the while loop)
void
EstablishedState::AppClose(TCPConnection* theConnection){
  /*theConnection->myTCPSender->sendFlags(0x11); // WHY NOT 01?
  theConnection->myState = FinWait1State::instance();
  theConnection->sendNext = theConnection->sendNext + 1;*/

  if (theConnection->RSTFlag) { // If we get reset flag we close

    theConnection->Kill();
    return;
  }

  if(theConnection->mySocket->isEof()){ // If no fin we go to close wait. (see book picture page 656)
    theConnection->myState = CloseWaitState::instance();
    theConnection->AppClose();
  } else { // if fin has not been received, we got finwait
    theConnection->myState = FinWait1State::instance();
    theConnection->myTCPSender->sendFlags(0x11); //Send FIN
    theConnection->finSent = true;
    theConnection->sendNext = theConnection->sendNext + 1;
  }


  //theConnection->Kill();

}

CloseWaitState*
CloseWaitState::instance()
{
  static CloseWaitState myInstance;
  return &myInstance;
}
void
CloseWaitState::AppClose(TCPConnection* theConnection){ // IF we receive FIN + ACK in established the state will change and this will be called
  theConnection->myTCPSender->sendFlags(0x11); // Send back FIN+ACK
  theConnection->myState = LastAckState::instance(); // Goes to last ack state.
  //theConnection->Kill();

}
// Handle close from application

//----------------------------------------------------------------------------
// add fin wait and stuff here.

FinWait1State*
FinWait1State::instance()
{
  static FinWait1State myInstance;
  return &myInstance;
}
void
FinWait1State::Acknowledge(TCPConnection* theConnection, udword theAcknowledgementNumber){
    if (theAcknowledgementNumber == theConnection->sendNext) // acknowledgenumber = next sequence number the receiver expects to receive
    {
      trace << "Acknowledge FinWait1State: correct" << endl;
      theConnection->myState = FinWait2State::instance();
    }
    else
    {
      theConnection->Kill();
      trace << "Acknowledge FinWait1State: wrong." << endl;
    }
}

//----------------------------------------------------------------------------
FinWait2State*
FinWait2State::instance()
{
  static FinWait2State myInstance;
  return &myInstance;
}

void
FinWait2State::NetClose(TCPConnection* theConnection)
{
  trace << "finwait2state::NetClose" << endl;


  theConnection->receiveNext += 1; // next expected sequence number updated from host.

  theConnection->myTCPSender->sendFlags(0x10); // According to picture from book we send ACK.

  //theConnection->mySocket->socketEof();
  theConnection->Kill(); // needed or not?? It's called in establishedstate::appclose which is called before.

}

//----------------------------------------------------------------------------
//


//----------------------------------------------------------------------------
//
LastAckState*
LastAckState::instance()
{
  static LastAckState myInstance;
  return &myInstance;
}

void
LastAckState::Acknowledge(TCPConnection* theConnection, udword theAcknowledgementNumber){
//  trace << "Lastackstate: last ack closing down ack & receivenext: " <<(dec) << (int) theConnection->receiveNext <<
  //" " << (dec) << (int) theAcknowledgementNumber << endl;
  if (theAcknowledgementNumber == theConnection->receiveNext) // Check H file for def of receivenext
  {
    trace << "lastackstate: ack same as receivenext" << endl;
  }
  else
  {
    trace << "lastackstate: ack not same as receivenext (WRONG?)" << endl;
  }

  theConnection->Kill(); // Should we kill here?
}

// Handle incoming Acknowledgement









//----------------------------------------------------------------------------
//
TCPSender::TCPSender(TCPConnection* theConnection,
                     InPacket*      theCreator):
        myConnection(theConnection),
        myAnswerChain(theCreator->copyAnswerChain()) // Copies InPacket chain! TCPInpacket in sendflags is the value of thecreator
{
}


TCPSender::~TCPSender()
{
  myAnswerChain->deleteAnswerChain();
}


void
TCPSender::sendFlags(byte theFlags) // Called in state::synchronize (state = any of the states like SynRecvdState, listenstate, etc..)
{
  // Decide on the value of the length totalSegmentLength.
  // Allocate a TCP segment.
  uword hoffs = myAnswerChain->headerOffset();
  uword totalSegmentLength = hoffs + TCP::tcpHeaderLength;
  byte* aReply = new byte[totalSegmentLength];
  // Calculate the pseudo header checksum
  aReply += hoffs; // Advances pointer to TCP packet

  TCPPseudoHeader* aPseudoHeader =
    new TCPPseudoHeader(myConnection->hisAddress,
                        TCP::tcpHeaderLength);
  uword pseudosum = aPseudoHeader->checksum();
  delete aPseudoHeader;
  // Create the TCP segment.

  TCPHeader* aHeader = (TCPHeader*) aReply;
  aHeader->sourcePort = HILO(myConnection->myPort);
  aHeader->destinationPort = HILO(myConnection->hisPort);
  aHeader->sequenceNumber = LHILO(myConnection->sendNext);
  aHeader->acknowledgementNumber = LHILO(myConnection->receiveNext);
  aHeader->headerLength = (byte) (TCP::tcpHeaderLength << 2);
  aHeader->flags = theFlags;
  aHeader->windowSize = HILO(myConnection->receiveWindow);
  // Calculate the final checksum.
  aHeader->checksum = 0; // WHY???
  aHeader->urgentPointer = 0;
  aHeader->checksum = calculateChecksum(aReply,
                                           TCP::tcpHeaderLength,
                                            pseudosum);


  // Send the TCP segment.
  // TCPInPacket::Decode (create TCPConnection) -> Creates TCPSender ->
  myAnswerChain->answer(aReply, TCP::tcpHeaderLength); // myAnswerChain = TCPInpacket::copyanswerchain() = IP::Answer (or a copy of IP answer)

  trace << "SENDING Tcp Packet FLAGS! Src port: " << myConnection->myPort << " Dest port: " <<
  myConnection->hisPort << " Seq#: " << myConnection->sendNext << " Ack#: " << myConnection->receiveNext << endl;

  //delete aReply; //Deallocate the dynamic memory
}

void
TCPSender::sendData(byte* theData, udword theLength){ // only difference from sendflags is that we got data, theLength and flags = 18 for estatblished
      trace << "TCPSender::sendData" << endl;
      //Same procedure as in sendFlags()
      //Allocate a TCP segment.
      uword hoffs = myAnswerChain->headerOffset();
      uword totalSegmentLength = hoffs + TCP::tcpHeaderLength + theLength;
      byte* aReply = new byte[totalSegmentLength];
      aReply += hoffs; // Advances pointer to TCP packet
      memcpy(aReply+TCP::tcpHeaderLength, theData, theLength); // Advances to the TCP data.
      // Calculate the pseudo header checksum
      TCPPseudoHeader* aPseudoHeader =
        new TCPPseudoHeader(myConnection->hisAddress,
                            TCP::tcpHeaderLength +theLength);
      uword pseudosum = aPseudoHeader->checksum();
      delete aPseudoHeader;
      // Create the TCP segment.

      TCPHeader* aHeader = (TCPHeader*) aReply;
      aHeader->sourcePort = HILO(myConnection->myPort);
      aHeader->destinationPort = HILO(myConnection->hisPort);
      aHeader->sequenceNumber = LHILO(myConnection->sendNext);
      aHeader->acknowledgementNumber = LHILO(myConnection->receiveNext);
      aHeader->headerLength = (byte) (TCP::tcpHeaderLength << 2);
      aHeader->flags = 0x18;
      aHeader->windowSize = HILO(myConnection->receiveWindow);
      // Calculate the final checksum.
      aHeader->checksum = 0;
      aHeader->urgentPointer = 0;
      aHeader->checksum = calculateChecksum(aReply, TCP::tcpHeaderLength + theLength, pseudosum);


      // Send the TCP segment.

      myConnection->sendNext += theLength;

      // Deallocate the dynamic memory
      trace << "SENDING Tcp Packet DATA! Src port: " << myConnection->myPort << " Dest port: " <<
    myConnection->hisPort << " Seq#: " << myConnection->sendNext << " Ack#: " << myConnection->receiveNext << endl;
    //sentMaxSeq is assigned the highest sequence number transmitted so far
    if (myConnection->sentMaxSeq < myConnection->sendNext) {
        myConnection->sentMaxSeq = myConnection->sendNext;
    }

    throwIndex++; // Throws away every 30 packet.
    //throwIndex = throwIndex % 100;
    //if (throwIndex == 30)
    if (throwIndex == 30)
    {
      throwIndex = 0;
      aReply -= hoffs;
      delete aReply;
    }
    else
    {
      // TCPInPacket::Decode (create TCPConnection) -> Creates TCPSender ->
      // myAnswerChain = TCPInpacket::copyanswerchain() = IP::Answer (or a copy of IP answer)

      myAnswerChain->answer(aReply, TCP::tcpHeaderLength + theLength);
    }


  // Send a data segment. PSH and ACK flags are set. (0x18)
}



/*
The responsibility of the method is to maintain the queue and send smaller sets
of data in segments by the method TCPSender::sendData until all data are
acknowledged.
The new method will initially be invoked from the method TCPConnection::Send.

*/

void
TCPSender::sendFromQueue() {
  trace << "TCPSender::sendFromQueue" << endl;
    // Usable window size = Window size per segment -
    udword theWindowSize = myConnection->myWindowSize -
    (myConnection->sendNext - myConnection->sentUnAcked); // lab manual
    if (theWindowSize > myConnection->myWindowSize) { // ??
      theWindowSize = 0;
      trace << "windowSize set to 0" << endl;
    }
    udword min = MIN(theWindowSize, myConnection->theSendLength());

    if (myConnection->sendNext < myConnection->sentMaxSeq) { //retransmit
      trace << "Retransmit" << endl;
      sendData(myConnection->theFirst(), myConnection->theSendLength());

    } else {
      while (min <= 0) {
        trace << "Transmitting" << endl;
        myConnection->windowSizeSemaphore->wait();
        theWindowSize = myConnection->myWindowSize - (myConnection->sendNext - myConnection->sentUnAcked);
        if (theWindowSize > myConnection->myWindowSize) {
          theWindowSize = 0;
        }
        min = MIN(theWindowSize, myConnection->theSendLength());

      }

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

void
TCPInPacket::decode(){
    TCPHeader *aHeader = (TCPHeader*) myData;
    //mysourceAddress is already defined in the TCPInPacket constructor
    mySourcePort = HILO(aHeader->sourcePort);
    myDestinationPort = HILO(aHeader->destinationPort);
    mySequenceNumber = LHILO(aHeader->sequenceNumber); // 32 bits
    myAcknowledgementNumber = LHILO(aHeader->acknowledgementNumber); // 32 bits
    TCPConnection* aConnection =  TCP::instance().getConnection(mySourceAddress,
                                                                mySourcePort,
                                                                myDestinationPort);

    if(!aConnection)
    {

      if ((aHeader->flags & 0x04) == 0x04) { // When reset received we don't do anything
        return; //no connection is establisehd,
      }

      if (TCP::instance().myConnectionList.Length() > 20) { // Having 20 is enough!
        delete myData;
        return;
      }
        aConnection = TCP::instance().createConnection(mySourceAddress,
                                          mySourcePort,
                                          myDestinationPort,
                                          this);
        trace << "Listening for syn flag" << endl;
        if((aHeader->flags & 0x02) != 0){
            // State LISTEN. Received a SYN flag.
            trace << "Found syn flag" << endl;
            aConnection->Synchronize(mySequenceNumber); // TCPconnection -> listenstate.synchronize
        }
        else{
            // State LISTEN. No SYN flag. Impossible to continue.
            trace << "Found no syn flag" << endl;
            aConnection->Kill();
        }
    }
    else{
        //STUFF
        // Connection was established. Handle all states.

        /*
        Close (passive): 1. Receive fin + ack, it will also send back fin and ack.
                         2. Ack
        */

      if ((aHeader->flags & 0x04) == 0x04) { //RST flag, we get a rst flag.
        aConnection->RSTFlagReceived();
        return;

        //cout << " successfully killed after rst flag" << endl;
      }

      //  trace << "Enter connection flags: " << (dec) << (int) aHeader->flags << endl;

        // Tells the sender how many bytes it can send before it has to
        // stop and wait for an acknowledge from the receiver.
        aConnection->myWindowSize = HILO(aHeader->windowSize); // Added lab 5.

        if ((aHeader->flags & 0x18) == 0x18)
          //STATE EstablishedState. ACK+PUSH received
        {
          trace << "ACK+PUSH received" << endl;
          aConnection->Receive(mySequenceNumber, myData+TCP::tcpHeaderLength, myLength-TCP::tcpHeaderLength);
        }

        else if ((aHeader->flags & 0x11) == 0x11)  // Receive FIN and ACK
          //STATE EstablishedState. FIN+ACK received
        {
          trace << "In connection receive: ACK + fin flag " << endl;
          aConnection->Acknowledge(myAcknowledgementNumber);
          aConnection->NetClose(); // In established state, set CloseWaitState, calls CloseWaitState::Appclose()
        }
        else if ((aHeader->flags & 0x10) == 0x10){ // ACK
          // STATE SynRecvdState. ACK received
          trace << "In connection receive: ACK flag " << endl;

          //aConnection->Acknowledge(myAcknowledgementNumber); // Old lab 5

          //Data is sent in Ack, if we already received this ack. (Duplicate Ack)
          if(myAcknowledgementNumber == aConnection->sentUnAcked){
            //cout << "Receiveing data in ACK on port: " << aConnection->hisPort << " size: " << myLength - TCP::tcpHeaderLength << endl;
            aConnection->Receive(mySequenceNumber, myData + TCP::tcpHeaderLength, myLength - TCP::tcpHeaderLength);
          } else {
            aConnection->Acknowledge(myAcknowledgementNumber);
          }

        }
      /*  if ((aHeader->flags & 0x04) == 0x04) { //RST flag
            trace << "RST FLAG" << endl;
            aConnection->Kill();
        }*/
    }
}

void
TCPInPacket::answer(byte* theData, udword theLength){

}

uword
TCPInPacket::headerOffset(){
    return myFrame->headerOffset() + TCP::tcpHeaderLength;
}

//----------------------------------------------------------------------------
//
InPacket*
TCPInPacket::copyAnswerChain()
{
  return myFrame->copyAnswerChain();
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
  //cout << "timer timed out!" << endl;
  myConnection->sendNext = myConnection->sentUnAcked; // lab manual set them to the same.
  myConnection->myTCPSender->sendFromQueue();
}


/****************** END OF FILE tcp.cc *************************************/
