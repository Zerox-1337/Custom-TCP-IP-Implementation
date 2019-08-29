#include "compiler.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
extern "C"
{
#include "system.h"
}

#include "iostream.hh"
#include "ip.hh"
#include "ipaddr.hh"
#include "icmp.hh"
#include "tcp.hh"




// Added tcp.hh here in lab 4.


#define trace if(false) cout
static udword incrementNum = 0;


IP::IP()
{
  myIPAddress = new IPAddress(0x82, 0xEB, 0xC8, 0x67); // 130.235.200.103
}

IP&
IP::instance()
{
  static IP myInstance;
  return myInstance;
}


const IPAddress&
IP::myAddress()
{
  return *myIPAddress;
}

IPInPacket::IPInPacket(byte* theData, udword theLength, InPacket* theFrame)  :
	InPacket(theData, theLength, theFrame)
{
}


void
IPInPacket::decode()
{
  // https://en.wikipedia.org/wiki/IPv4 information about the values.
  IPHeader *aHeader = (IPHeader*) myData;

	if (aHeader->destinationIPAddress == IP::instance().myAddress()) {
		trace << "decoding IP packet" << endl;

		//All TCP/IP are big endian, but only, 32 bit fields or like other bits are not affected.
    // Bit 0-3 = Version, 4-7 = HeaderLength (BIG ENDIAN, most significant bit first)
    // 16-31 total length
    // 48-63: Fragment offset, FIRST three is flags.

		byte versionIP = aHeader->versionNHeaderLength >> 4; // Performs 4 bitwise right shift, to internet header length
		byte headerLength = aHeader->versionNHeaderLength & 0x0F; // Version(1-4) (header 1-4) & 0000 1111 (1's are 4-7 it's in big endian)

		uword totalLength = HILO(aHeader->totalLength);
		byte flags = aHeader->fragmentFlagsNOffset >> 13; // 0000 0000 0000 0 flags (1-3) Removes the fragment offset to get flags.


    myProtocol = aHeader-> protocol; // Saves them in IPInpacket class.
    mySourceIPAddress = aHeader->sourceIPAddress;


		trace << "destination Ip Address: " << aHeader->destinationIPAddress << endl; // 0011 1111 1111 1111 & flags (1-3 bits) 0 0000 0000 0000.
		if (versionIP == 4 && headerLength == 5 && (flags & 0x3FFF) == 0) { // Flags & 0x3FFF check so packet not fragmentented.
			trace << "ipv4, headerlength = 5, flag = 0" << endl;

			if (myProtocol == 1) { //icmp has 1 as protocol value
				    trace << "icmp protocol detected" << endl;
				        ICMPInPacket *aPacket = new ICMPInPacket(myData + IP::ipHeaderLength, // advance pointer past ip header
                                              totalLength - IP::ipHeaderLength, // Remove IP header from packet length.
                                              this); // 20 = IP header length
				aPacket->decode();
				delete aPacket;
			} else if (aHeader->protocol == 6) { //tcp has protcol number 6.
				      trace << "tcp protocol detected" << endl; // Below for next labs.
              TCPInPacket *aPacket = new TCPInPacket(myData + IP::ipHeaderLength,
                                              totalLength - IP::ipHeaderLength,
                                              this,
                                              mySourceIPAddress);
              aPacket->decode();
              delete aPacket;
			}
		}
	}
}



void
IPInPacket::answer(byte* theData, udword theLength)
{
  trace << "IPInPacket::answer running" << endl;
  // Length = theLength + this->headeroffset = thelength (all data) + ethernet length + IPHeaderLength.
  // Why is ethernet length included here? Check ICMP decode/answer, it builds it up again.
	theData = theData - IP::ipHeaderLength; // Opposite of decode, go up instead of down.
  theLength = theLength + IP::ipHeaderLength; // Not needed convered in ICMP
	IPHeader* aHeader = (IPHeader*) theData;
	aHeader->versionNHeaderLength = ((4 << 4) | 5);
	aHeader->typeOfService = 0; // Everything with 16 bits have to be read from the other way = hilo
	aHeader->totalLength = HILO(theLength); // Removes ethenernetheader length from here. Which gives total length of IP packet
	aHeader->identification = HILO(incrementNum); // An increasing static number.
	incrementNum++; // Increase icrementing number.
	aHeader->fragmentFlagsNOffset = 0;
	aHeader->timeToLive = 64;
	aHeader->protocol = myProtocol;
	aHeader->headerChecksum = 0;
	aHeader->sourceIPAddress = IP::instance().myAddress();
	aHeader->destinationIPAddress = mySourceIPAddress;

	byte headerLength = (aHeader->versionNHeaderLength & 0x0F)*4;
	aHeader->headerChecksum = calculateChecksum(theData, headerLength);
  trace << "new checksum in ip: " <<hex << int(aHeader->headerChecksum) << endl;
	myFrame->answer(theData, theLength);
}


uword
IPInPacket::headerOffset()
{
	return myFrame->headerOffset() + IP::ipHeaderLength;
}






InPacket*
IPInPacket::copyAnswerChain()
{
  IPInPacket* anAnswerPacket = new IPInPacket(*this);
  anAnswerPacket->setNewFrame(myFrame->copyAnswerChain());
  return anAnswerPacket;
}
