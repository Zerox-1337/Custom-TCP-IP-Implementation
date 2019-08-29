/*!***************************************************************************
*!
*! FILE NAME  : Ethernet.cc
*!
*! DESCRIPTION: Handles the Ethernet layer
*!
*!***************************************************************************/

/****************** INCLUDE FILES SECTION ***********************************/

#include "compiler.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
extern "C"
{
#include "msg.h"
#include "osys.h"
#include "system.h"
#include "etrax.h"
#include "xprintf.h"
}

#include "iostream.hh"
#include "frontpanel.hh"
#include "ethernet.hh"
#include "llc.hh"
#include "sp_alloc.h"

//#define D_ETHER

#ifdef D_ETHER
#define trace cout
#else
#define trace if(false) cout
#endif

#define trace2 if(false) cout

//#define trace cout

/********************** LOCAL VARIABLE DECLARATION SECTION *****************/

static bool processingPacket    = FALSE;    /* TRUE when LLC has a packet */
static bool bufferFullCondition = FALSE;    /* TRUE when buffer is full   */
//static int counterE = 0;
/****************** Ethernet DEFINITION SECTION *************************/

//----------------------------------------------------------------------------
//
Ethernet::Ethernet()
{
  trace << "Ethernet created." << endl;
  nextRxPage = 0;
  nextTxPage = 0;
  processingPacket = FALSE;

  myEthernetAddress = new EthernetAddress(0x00, 0x19, 0x94, 0x06, 0x26, 0x44); //Pointer myEthernetAddress points to EthernetAddress.
  // STUFF: Set myEthernetAddress to your "personnummer" here!

  this->initMemory();
  this->initEtrax();
  //cout << "My node address is " << this->myAddress() << endl;
}

//----------------------------------------------------------------------------
//
Ethernet&
Ethernet::instance()
{
  static Ethernet myInstance;
  return myInstance;
}

//----------------------------------------------------------------------------
//
const EthernetAddress&
Ethernet::myAddress()
{
  return *myEthernetAddress;
}

//----------------------------------------------------------------------------
//
void
Ethernet::initMemory() // EDITS MADE BY US
{
  int page;
  BufferPage* aPointer;

  trace << "initMemory" << endl;


  // STUFF: Set status byte on each page to 0 here!
  // Shall be done for both rx buffer and tx buffer.

  aPointer = (BufferPage *)txStartAddress; // Converts a byte pointer 4 bits to BufferPage pointer 32 bits. Usually not good to do
  for (page = 0; page < txBufferPages; page++)
  {
	  aPointer->statusCommand = 0x00;
	  aPointer++; // Will go to the next pointer address automatically.
  }

  aPointer = (BufferPage *)rxStartAddress;
  for (page = 0; page < rxBufferPages; page++)
  {
	  aPointer->statusCommand = 0x00;
	  aPointer++; // Will go to the next pointer address automatically.
  }



}

//----------------------------------------------------------------------------
//
void
Ethernet::initEtrax()
{
  trace << "initEtrax" << endl;
  DISABLE_SAVE();

  *(volatile byte *)R_TR_MODE1 = (byte)0x20;
  *(volatile byte *)R_ANALOG   = (byte)0x00;

  *(volatile byte *)R_TR_START = (byte)0x00;
  // First page in the transmit buffer.
  *(volatile byte *)R_TR_POS   = (byte)0x00;
  // transmit buffer position.
  //                         |                     |
  //           20 19 18 17 16 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1 0
  // R_TR_START                0  0  0  0  0  0 0 0                 first page is 0
  // R_TR_POS   0  0  0  0  0  0  0  0                              0x40000000
  //
  // R_TR_START = 0x00 = bit 15:8 of txStartAddress.
  // R_TR_POS   = 0x00 = bit 20:13 of txStartAddress.

  *(volatile byte *)R_RT_SIZE = (byte)0x0a;
  // rx and tx buffer size 8kbyte (page 41, 43)
  // rx: xxxxxx10 OR tx: xxxx10xx = xxxx1010

  *(volatile byte *)R_REC_END  = (byte)0xff;
  // Last page _in_ the receive buffer.
  *(volatile byte *)R_REC_POS  = (byte)0x04;
  // receive buffer position.
  //                         |                     |               |
  // Bit:      20 19 18 17 16 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1 0
  // REC_END                   1  1  1  1  1  1 1 1                 last page in buffer is 31 (!)
  // R_REC_POS  0  0  0  0  0  1  0  0                              0x40008000
  //
  // REC_END = 0x3f = bit 15:8 of (rxStartAddress + 31*256).
  // R_REC_POS = 0x01 = bit 20:13 of rxStartAddress.

  *(volatile byte *)R_GA0 = 0xff;     /* Enable ALL multicast */
  *(volatile byte *)R_GA1 = 0xff;
  *(volatile byte *)R_GA2 = 0xff;
  *(volatile byte *)R_GA3 = 0xff;
  *(volatile byte *)R_GA4 = 0xff;
  *(volatile byte *)R_GA5 = 0xff;
  *(volatile byte *)R_GA6 = 0xff;
  *(volatile byte *)R_GA7 = 0xff;

  // Reverse the bit order of the ethernet address to etrax.
  byte aAddr[EthernetAddress::length];
  myEthernetAddress->writeTo((byte*)&aAddr);

  byte aReverseAddr[EthernetAddress::length];

  for (int i = 0; i < EthernetAddress::length; i++)
  {
    aReverseAddr[i] = 0;
    for (int j = 0; j < 8; j++)
    {
      aReverseAddr[i] = aReverseAddr[i] << 1;
      aReverseAddr[i] = aReverseAddr[i] | (aAddr[i] & 0x01);
      aAddr[i] = aAddr[i] >> 1;
    }
  }

  *(volatile byte *)R_MA0 = aReverseAddr[0];     /* Send this first */
  *(volatile byte *)R_MA1 = aReverseAddr[1];
  *(volatile byte *)R_MA2 = aReverseAddr[2];
  *(volatile byte *)R_MA3 = aReverseAddr[3];
  *(volatile byte *)R_MA4 = aReverseAddr[4];
  *(volatile byte *)R_MA5 = aReverseAddr[5];      /* Send this last */

  *(volatile byte *)R_TR_MODE1  = 0x30;   /* Ethernet mode, no loop back */
  *(volatile byte *)R_ETR_MASKC = 0xbf;   /* Disable all other etr interrupts*/
  *(volatile byte *)R_BUF_MASKS = 0x0f;   /* Enable all buf. interrupts */

  *(volatile byte *)R_TR_MODE2 = 0x14;    /* Start network interface */

  do { asm volatile ("movem sp,[0xc0002fff]");} while (FALSE);

  *(volatile byte *)R_REC_MODE = 0x06;    /* Clear receiver interrupts */

  RESTORE();
}

//----------------------------------------------------------------------------
//

// MJ : external packet interrupt received.
extern "C" void
ethernet_interrupt()
{

  byte bufferStatus = *(volatile byte *)R_BUF_STATUS;
  /* Therer are four possible interrupts to handle here: */
  /* 1. Packet received, packet available from the network                                  */
  /* 2. Receive buffer full, entire buffer is in use                              */
  /* 3. Packet transmitted, all outgoing packets are transmitted                               */
  /* 4. Exessive retry, generated after 15 consecutive failrues to transmit                                  */
  /* They have one bit each in the R_BUF_STATUS register */

  /*-------------------------------------------------------------------------*/
  /* 1. Packet received interrup. */
  if (BITTST(bufferStatus, BUF__PACKET_REC))
  {
    *(volatile byte *)R_REC_MODE = 0x04;
    // Ack packet received interupt
    if (!processingPacket)
    {
      if (Ethernet::instance().getReceiveBuffer())
      {
        processingPacket = TRUE;
        os_int_send(OTHER_INT_PROG, THREAD_MAIN, THREAD_PACKET_RECEIVED,
                    NO_DATA, 0, NULL);
      }
      else
      {
        processingPacket = FALSE;
      }
    }
  }

  /*--------------------------------------------------------------------------*/
  /* 2. When we receive a buffer full interrupt, we must not clear the inter- */
  /* rupt until there is more space in the receive buffer. This is done in    */
  /* return_rxbuf(). We clear the buffer full interrupt mask bit to disable   */
  /* more interrupts until things are taken care of in return_rxbuf().        */
  if (BITTST(bufferStatus, BUF__BUFFER_FULL))
  {
    *(volatile byte *)R_BUF_MASKC = 0x04;
    /* Disable further interrupts until we */
    /* are given space in return_rxbuf()   */
    *(volatile byte*)R_REC_MODE = 0x02;
    /* Acknowledge buffer full interrupt */
    bufferFullCondition = TRUE;
	//cout << "buffer full " << endl;
  }

  /*--------------------------------------------------------------------------*/
  /* 3. This interrupt means that all packets in ring buffer have been sent   */
  if (BITTST(bufferStatus, BUF__PACKET_TR))
  {
    *(volatile byte *)R_TR_CMD = 0;
    /* Clear interrupt, nothing else */
  }

  /*--------------------------------------------------------------------------*/
  /* 4. If we get an excessive retry interrupt, we reset the transmitter.     */
  if (BITTST(bufferStatus, BUF__ABORT_INT))
  {
    Ethernet::instance().resetTransmitter();
  }
}

//----------------------------------------------------------------------------
//
bool
Ethernet::getReceiveBuffer() // Called from interrupt method which is triggered when a packet interrupt is received.
{



  // STUFF: lots to do here!

 // The first page in the received packet is given by nextRxPage.
 // The first page starts at address 'rxStartAddress + (nextRxPage * 256)',

	// rxStartAddress = Start address of buffer that contains 128 pages = 128*256 bytes.
	// nextRxPage = It's really current page. Gets the offset for the pages. Starts at 0 in the constructor is increased by some return rx method
	// aPointer->endPointer = the end point of package. If the package is closed to the end and can't fit in the last few pages it wraps around to the beginning.


	// Creates a buffer page pointer at start address of current page. This is also the start of the package since every package begins at the nextRxPage start.
	BufferPage* aPointer;
	aPointer = (BufferPage *)(rxStartAddress + (nextRxPage * 256));  // Start pointer.

  //BufferPage* aPage; // Another group used apage in statuscommand and added minus 4 to the lengths
  //aPage = (BufferPage *)rxStartAddress;

	// If anyone of the following
  //cout << (dec) << (int) (aPointer->statusCommand) << endl;
	//trace << "GetReceiveBuffer: Outside IF" << endl; // added by me
 if ((aPointer->statusCommand == 0x01) || // Packet available
      (aPointer->statusCommand == 0x03))   // Packet available and buffer full
  {
	  //trace << "GetReceiveBuffer: inside IF" << endl; // Added by me
    // use endptr to find out where the packet ends, and if it is wrapped.

	// Compares the start Pointer with the endpointer. When comparing pointers its the address they point to that is compared.
    //if (aPointer < (BufferPage *)(aPointer->endPointer + endPtrOffset)) // EndPointer doesn't contain the rxstartaddress but it does contain the offset between the transmitt and receive buffer. Which is why txStart needs to be added.
    if ( (nextRxPage * 256) < (aPointer->endPointer - rxBufferOffset)) // COMPARE: No rxBufferOfset
    { // endPtrOffset = txStartAddress
	    //trace << "GetReceiveBuffer: Normal packet" << endl; // ADDED BY ME
	    // one chunk of data

      data1   = aPointer->data;
      length1 = aPointer->endPointer - rxBufferOffset - (nextRxPage * 256) - commandLength;// - crcLength; // The first 4 bytes (command) and last 4 (CRC) are not the packet, so thus there length shouldn't be included.
      //length1 = aPointer->endPointer - rxBufferOffset - (dword) data1;
      data2   = NULL;
      length2 = 0;
    }
    else
    {
      // two chunks of data
		//trace << "GetReceiveBuffer: Wrrapped" << endl; // ADDED BY ME
      data1   = aPointer->data;
      length1 =  ((rxBufferPages -nextRxPage)*256) -crcLength; // Package always start. crc won't come until after wrap.
      //length1 = rxStartAddress + rxBufferSize - (udword) data1;
      data2   = (byte *) (rxStartAddress); // To get to the data
      length2 = aPointer->endPointer - rxBufferOffset; // You wrap around so you don't have command length anymore.
    }

    return true;
  }



#ifdef D_ETHER
  //printf("No packet found.\r\n"); // cannot use cout in interrupt context...
#endif
  return false; // Should be false.



}


//----------------------------------------------------------------------------
//
void
Ethernet::returnRXBuffer()
{
  DISABLE_SAVE();
  BufferPage* aPage = (BufferPage *)(rxStartAddress + (nextRxPage * 256));
  /* Pointer to beginning of packet to return. nextRxPage is the packet just */
  /* consumed by LLC.                                                        */

  uword endInBuffer = aPage->endPointer - rxBufferOffset;
  // end pointer from start of receive buffer. Wraps at rxBufferSize.

  if ((aPage->endPointer - rxBufferOffset) < (nextRxPage * 256))
  {
    trace << "Delete wrap copy" << endl;
    delete [] wrappedPacket;
  }

  // The R_REC_END should point to the begining of the page addressed by
  // endInBuffer.
  uword recEnd = (endInBuffer + rxBufferOffset) >> 8;
  *(volatile byte *)R_REC_END = recEnd;

  trace << "RetRx: aPage " << hex << (udword)aPage
        << " endMarker " << endInBuffer
        << " recEnd " << recEnd
        << endl;
  if (bufferFullCondition)
  {
    trace << "buffer was full" << endl;
    *(volatile byte *)R_BUF_MASKS = 0x04;
    /* Enable this interrupt again. */
    bufferFullCondition = FALSE;
  }

  // Adjust nextRxPage
  uword endPage = endInBuffer >> 8;
  nextRxPage = (endPage + 1) % rxBufferPages;
  trace << " endPage " << hex << endPage << " nextRxPage "
        << (uword)nextRxPage << endl;

  // Process next packet (if there is one ready)
  if (this->getReceiveBuffer())
  {
    trace << "Found pack in retrx" << endl;
    processingPacket = TRUE;
    RESTORE();
    os_send(OTHER_INT_PROG, THREAD_MAIN, THREAD_PACKET_RECEIVED,
                NO_DATA, 0, NULL); // Sends to the main method in initcc and decode is called.
  }
  else
  {
    processingPacket = FALSE;
    RESTORE();
  }


}

//----------------------------------------------------------------------------
//
void
Ethernet::decodeReceivedPacket()
{
  trace << "Found packet at:" << hex << (udword)data1 << dec << endl;

  //cout << ax_coreleft_total() << endl;
  //Ethernet::returnRXBuffer(); // Everything here is just temp code to try network led. Upload the code multiple times.


  // STUFF: Blink packet LED
  //Ethernet::instance().returnRXBuffer();
  FrontPanel::instance().packetReceived();
  //cout << (dec) << counterE++ << " " << ax_coreleft_total() << endl;
  //cout << ax_coreleft_total() << endl;
  // STUFF: Create an EternetInPacket, two cases:
  EthernetInPacket* aPacket;
  InPacket *theFrame = NULL; // Shouldn't be set to just 0, should be a frame that points to null.

  // REMOVe the comments below on code.

  if (data2 == NULL)
  {
    trace << "Not wrap" << endl;
	  // STUFF: Create an EternetInPacket
	 aPacket = new EthernetInPacket(data1, length1, theFrame); // WHHY SET a class myframe to just 0?


  }
  else
  {
    trace << "Wrap copy" << endl;
    // When a wrapped buffer is received it will be copied into linear memory
    // this will simplify upper layers considerably...
    // There can be only one mirrored wrapped packet at any one time.
    wrappedPacket = new byte[length1 + length2];
    memcpy(wrappedPacket, data1, length1);
    memcpy((wrappedPacket + length1), data2, length2);
    // STUFF: Create an EternetInPacket
	aPacket = new EthernetInPacket(wrappedPacket, length1 + length2, theFrame); // WHHY SET TO 0.
  }
  // STUFF: Create and schedule an EthernetJob to decode the EthernetInPacket

  EthernetJob* ethernetJobe = new EthernetJob(aPacket);
  Job::schedule(ethernetJobe); // EthernetJob at the beginning or just job???

  //delete aPacket; // Should we remove??? Called in the doit method after the package have been decoded.

}
//----------------------------------------------------------------------------
//
void
Ethernet::transmittPacket(byte *theData, udword theLength)
{
  trace << "transmitt" << endl;
  // Make sure the packet fits in the transmitt buffer.

  /* If the packet ends at a 256 byte boundary, the next buffer is skipped  */
  /* by the Etrax. E.g. a 508 byte packet will use exactly two buffers, but */
  /* buffer_use should be 3.                                                */
  uword nOfBufferPagesNeeded = ((theLength + commandLength) >> 8) + 1;

  word availablePages;
  // availablePages can be negative...
  // Make sure there is room for the packet in the transmitt buffer.
  uword timeOut = 0;
  do
  {
    /* Transmitter is now sending at page r_tr_start. */
    /* May not be the same as nextTxPage */
    /* Bits <15:8>, page number */
    byte r_tr_start  = *(volatile byte*)R_TR_START;

    // Set sendBoundary to the page before r_tr_start page
    word sendBoundary =  r_tr_start - 1;
    if (sendBoundary < 0)
    {
      sendBoundary = txBufferPages - 1;  /* Set to last page */
    }

    availablePages = sendBoundary - nextTxPage;
    /* In 256 byte buffers */
    if (availablePages < 0)
    {
      availablePages += txBufferPages;
      /* Boundary index is lower than nextTxPage */
    }
#ifdef ETHER_D
    ax_printf("sendBoundary:%2d, nextTxPage:%2d, availablePages:%2d, "
              "packet:%4d bytes = %d pages\n", sendBoundary,
              nextTxPage, availablePages, theLength, nOfBufferPagesNeeded);
#endif
    if((uword)availablePages < nOfBufferPagesNeeded)
    {
      //cout << "Tx buffer full, waiting." << endl;
      os_delay(1);                                       /* One tic = 10 ms */
      timeOut++;
      /* If nothing happens for 3 seconds then do something drastical.      */
      if (timeOut > 3 * WAIT_ONE_SECOND)                       /* 300 tics */
      {
        //cout << "Transmit buffer full for 3 seconds. Resetting.\n";
        this->resetTransmitter();
        timeOut = 0;
      }
    }
  }
  while((uword)availablePages < nOfBufferPagesNeeded);

  // There is room for the packet in the transmitt buffer
  // Copy the packet to the transmitt buffer
  // Remember: Undersized packets must be padded! Just advance the end pointer
  // accordingly.

  // STUFF: Find the first available page in the transmitt buffer
  BufferPage* aPointer; // Start of a page
	aPointer = (BufferPage *)(txStartAddress + (nextTxPage * 256));

  // One page is 4 bytes for command and then data contains theData = header and data that was sent in from answer
  if (nextTxPage + nOfBufferPagesNeeded <= txBufferPages)
  {
    // STUFF: Copy the packet to the transmitt buffer
    // Simple case, no wrap

    // Memcpy(Destination,Source,Num)
    // Destination = pointer to the destination array where the content is to be copied.
    // Source = Pointer to the source of data to be copied
    // Numb = number of bytes to copy.

    // Copies the data into the buffer page class
    memcpy(aPointer->data, theData, theLength); // Look at picture as well.

    if (theLength <  (minPacketLength + Ethernet::ethernetHeaderLength )) // Might not need headerOffset and crc
    {
      theLength = minPacketLength + Ethernet::ethernetHeaderLength; // We assume the hardware takes care of CRC?
      //cout << "Pad undersized" << endl;
    }
    // Pad undersized packets
    // (nextTxPage * 256) + commandLength will be same as data address?
    // Points to the last address of the packet.
    aPointer->endPointer = (nextTxPage * 256) + commandLength + theLength -1; // Index = length -1
    //aPointer->endPointer = (udword) aPointer->data + theLength -1;
    // If padded, it will point to the minimum required length, the data in between is not important?
  }
  else
  {
    trace << "Warped transmission" << endl;
    // STUFF: Copy the two parts into the transmitt buffer, cannot be undersized
    uword length1 =  ((txBufferPages -nextTxPage)*256); // minus crcLength (was at 2018-02-19) since it wraps
    //uword length1 =  (txStartAddress + txBufferSize) - (udword) aPointer->data;
    uword length2 = theLength - length1; // Gets the rest.
    //wrappedPacket = new byte[length1 + length2];
    memcpy(aPointer->data, theData, length1);
    memcpy((byte *)txStartAddress, theData + length1, length2);
    aPointer->endPointer = length2-1; // Index = length -1
  }

  /* Now we can tell Etrax to send this packet. Unless it is already      */
  /* busy sending packets. In which case it will send this automatically  */

  // STUFF: Advance nextTxPage here!
  //nOfBufferPagesNeeded = Used above in the code to get how many pages are needed based on the packet length (not our code, it was already in the method)
  nextTxPage = (nextTxPage + nOfBufferPagesNeeded) % txBufferPages; // Mod with the number of pages since after we reach the end we will wrap back to the beginning.
  // Tell Etrax there isn't a packet at nextTxPage.
  BufferPage* nextPage = (BufferPage*)(txStartAddress + (nextTxPage * 256));
  nextPage->statusCommand = 0; // Basically resets everything so the next packet can be received
  nextPage->endPointer = 0x00;  // Why setting this to 0? Maybe because there is no packet and there is no last byte if it has nothing.

  // STUFF: Tell Etrax to start sending by setting the 'statusCommand' byte of
  //the first page in the packet to 0x10!
  aPointer->statusCommand = 0x10; // Extrax will send this byte. This is value is checked in receivebuffer.

  delete [] theData; // Clears the data.
  *(volatile byte*)R_TR_CMD = 0x12;
}

//----------------------------------------------------------------------------
//
void
Ethernet::resetTransmitter()
{
  //cout << "Transmit timeout. Resetting transmitter.\n" << endl;
  *(volatile byte*)R_TR_CMD = 0x01;             /* Reset */
  *(volatile byte*)R_TR_START = 0x00;  /* First to transmit */

  nextTxPage = 0;

  int page;
  BufferPage* aPointer;
  aPointer = (BufferPage *)txStartAddress;
  for (page = 0; page < txBufferPages; page++)
  {
    aPointer->statusCommand = 0;
    aPointer++; // Will go to the next pointer address automatically.
  }


}

// STUFF: Add EthernetJob implementation
EthernetJob::EthernetJob(EthernetInPacket * thePacket) // Changed 2018-02-19
//Job(), // Added since frontpanel worked this way
//myPacket(thePacket)
{
	myPacket = thePacket; // Removed need to be called before constructor like frontpanel
}

// Decode myPacket
void
EthernetJob::doit()
{
	myPacket->decode(); // Ethernet packet calls its decode
	delete myPacket; // This deletes the ethernet packet when job done.
}

// STUFF: Add EthernetInPacket implementation

// TheData = points to first byte of package aka header + data only. (Header is start)
EthernetInPacket::EthernetInPacket(byte * theData, udword theLength, InPacket * theFrame) :
	InPacket(theData, theLength, theFrame) // Ethernetinpacket is a sub class of inpacket, calls its constructor
{

}

uword
EthernetInPacket::headerOffset()
{
	return Ethernet::ethernetHeaderLength;
	//return myFrame->headerOffset() + 0; // Why + 0?
}

void
EthernetInPacket::decode()
{
	 // myData, mylength = Inpacket
	// myTypelen, source and destination = ethernetInPacket
  // Gets the header from memory, and set destination adress of ethernetinpacket.
  EthernetHeader* aHeader = (EthernetHeader*) myData; // Gets a ethernetHeader from myData inpacket that contains everything.
	// Gets Sets the values of the ethernet package from the inpacket mydata.
	myTypeLen =  ((aHeader->typeLen & 0x00ff) << 8 ) | ((aHeader->typeLen & 0xff00) >> 8 ); // Bits are in wrong order in etrax (this shitty axis thing) THIS IS FOR 16 bits only, check h files for other commands
  myDestinationAddress = aHeader->destinationAddress;
	mySourceAddress = aHeader->sourceAddress;




  // COMMENTS MIGHT BE WRONG.
	LLCInPacket* aPacket = new LLCInPacket(myData + headerOffset(), // Pointer to LLCInPacket, it's inside ethernetheader
		myLength - headerOffset() - Ethernet::crcLength, // LLC packet length is data length of ethernet.
		this,
		myDestinationAddress,
		mySourceAddress,
    myTypeLen); // The frame is a reference to the upper layer, so ethernetpacket is upper and llc package is lower.
	aPacket->decode(); // Calls the LLCInPRAcket decode that then calls EtheernetInPacket answer.
  delete aPacket; // Added again, we need to delete the LLC packet because we are done with it

  // In eterhetnJob::doIt the LLC packet is deleted.
	Ethernet::instance().returnRXBuffer();
}

void
EthernetInPacket::answer(byte * theData, udword theLength) // Call order (so called inside llc): EthernetInPakcet::Decode -> LLC::Decode - > ethernetInPacket::Answer
{ // see picture: This is caled in LLC in (decode). Our next move is to called transmitt buffer somewhere
  //cout << "length " << (dec) << theLength << endl;
        theData = theData - headerOffset();
        theLength += headerOffset(); // LAB 6 has this.
        EthernetHeader *aHeader = new EthernetHeader();
        aHeader->destinationAddress = mySourceAddress;
        aHeader->sourceAddress = Ethernet::instance().myAddress(); // It's possible we might get broadcast so swapping is not good
        aHeader->typeLen = ((myTypeLen & 0x00ff) << 8) | ((myTypeLen & 0xff00) >> 8);
        memcpy(theData, aHeader, headerOffset());

        Ethernet::instance().transmittPacket(theData, theLength);
        delete aHeader;
  /*
  We create a byte array that stores the whole ethernet package.
  We have the data from the LLC part and we want to add an ethernet header before it in memory
  then return that data in the right order to transmitt buffer.
  */
  //cout << ax_coreleft_total() << endl;
        //uword ethernetLength = theLength + headerOffset(); // LLC packet length + ethernetHeader length
        //byte *packetSend = new byte[ethernetLength]; // byte array of length Ethernetlength
  // Header. Headoffset = 14.
  // PacketSend 0-5: destinationAddress.
  // destination = the source address we had in decode


        //mySourceAddress.writeTo(packetSend);
  // does the same thing as commented below

  /* Same thing but way more easier to understand.
  packetSend[0] = mySourceAddress[0];
  packetSend[1] = mySourceAddress[1];
  packetSend[2] = mySourceAddress[2];
  packetSend[3] = mySourceAddress[3];
  packetSend[4] = mySourceAddress[4];
  packetSend[5] = mySourceAddress[5];*/

  // packetSend 6-11: source address.
        //EthernetAddress returningSourceAddress = Ethernet::instance().myAddress();
        //returningSourceAddress.writeTo(packetSend + mySourceAddress.length);
  /* does the same thing as commented below
    packetSend + myDestinationAddress.length = packetSend[6] = Start of source
    Puts my destination address into packetSend[6].
  */
  /* Same thing but way easier to understand.
  packetSend[6] = myDestinationAddress[0];
  packetSend[7] = myDestinationAddress[1];
  packetSend[8] = myDestinationAddress[2];
  packetSend[9] = myDestinationAddress[3];
  packetSend[10] = myDestinationAddress[4];
  packetSend[11] = myDestinationAddress[5];*/

  // packetSend 12-13: TypeLen. at position 12 we put in byte array with length 2.
        //packetSend[12] = ((myTypeLen & 0x00ff) << 8 ) | ((myTypeLen & 0xff00) >> 8 );

  // Rest, the data from LLC at position 14. headerOffset = 14.
        //memcpy(packetSend + headerOffset(), theData, theLength); // changed in lab 3, got a warning.
  //packetSend[headerOffset()] = theData;



  /*ARPHeader* aHeader = (ARPHeader*) myData;
  trace2 << "ARP out "<< endl;
  trace2 << "Sender mac: " << aHeader->senderEthAddress << endl;
  trace2 << "Sender IP: " << aHeader->senderIPAddress << endl;
  trace2 << "Target mac: " << aHeader->targetEthAddress << endl;
  trace2 << "Target IP: " << aHeader->targetIPAddress << endl;*/
  //trace2 << ((myTypeLen & 0x00ff) << 8 ) | ((myTypeLen & 0xff00) >> 8 ) << endl;
//PacketSend + 1 = PacketSend[1]


  //MySourceAddress.writeTo(packetSend); // MySourceAddress is put into the start of packetSent
  // Data rest

      //Ethernet::instance().transmittPacket(packetSend, ethernetLength);
      //delete packetSend; // We only added it into memory to add the ethernetHeader before the data.
      //    theData = theData - headerOffset(); // theData is only the LLC data (see decode), we need to clear the ethernetheader from memory as well or we will get memory leaks.
      //    delete theData;
  // We don't need it anymore afterwards.

  // Transmitt buffer method should be called here accoording to picture
}

InPacket*
EthernetInPacket::copyAnswerChain()
{
  EthernetInPacket* anAnswerPacket = new EthernetInPacket(*this);
  anAnswerPacket->setNewFrame(NULL);
  return anAnswerPacket;
}

//----------------------------------------------------------------------------
//
EthernetAddress::EthernetAddress()
{

}

//----------------------------------------------------------------------------
//
EthernetAddress::EthernetAddress(byte a0, byte a1, byte a2, byte a3, byte a4, byte a5)
{
  myAddress[0] = a0;
  myAddress[1] = a1;
  myAddress[2] = a2;
  myAddress[3] = a3;
  myAddress[4] = a4;
  myAddress[5] = a5;
}

//----------------------------------------------------------------------------
//
bool
EthernetAddress::operator == (const EthernetAddress& theAddress) const
{
  for (int i=0; i < length; i++)
  {
    if (theAddress.myAddress[i] != myAddress[i])
    {
      return false;
    }
  }
  return true;
}

//----------------------------------------------------------------------------
//
void
EthernetAddress::writeTo(byte* theData)
{
  for (int i = 0; i < length; i++)
  {
    theData[i] = myAddress[i];
  }
}

//----------------------------------------------------------------------------
//
ostream& operator <<(ostream& theStream, const EthernetAddress& theAddress)
{
  static char aString[6*2+1];
  int i;

  aString[0] = '\0';
  for (i = 0; i < 5; i++)
  {
    //sprintf(aString, "%s%02x:", aString, theAddress.myAddress[i]);
  }
  //sprintf(aString, "%s%02x", aString, theAddress.myAddress[i]);
  theStream << aString;
  return theStream;
}

//----------------------------------------------------------------------------
//
EthernetHeader::EthernetHeader()
{
}


/****************** END OF FILE Ethernet.cc *********************************/
