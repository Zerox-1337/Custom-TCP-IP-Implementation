#include "compiler.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


#include "iostream.hh"
#include "frontpanel.hh"
#include "ethernet.hh"
#include "llc.hh"
#include "ip.hh"
#include "sp_alloc.h"

#include "arp.hh"

#define trace if(false) cout

ARPInPacket::ARPInPacket(byte* theData, udword theLength, InPacket* theFrame)  :
	InPacket(theData, theLength, theFrame)
{


}
void
ARPInPacket::decode()
{
	ARPHeader* aHeader = (ARPHeader*) myData; // Gets a ARP from myData inpacket that contains everything.
	//cout << "ARP decode "<< endl;
	if (aHeader->targetIPAddress == IP::instance().myAddress()) // Check if it's to us.
	{
		// Copied from LLC changed myframe-> to this because it's more clearer
		uword hoffs = this->headerOffset();
		byte* temp = new byte[myLength + hoffs];
		byte* aReply = temp + hoffs; // starts at the length.
		memcpy(aReply, myData, myLength);
		ARPHeader *replyHeader = (ARPHeader* ) aReply;

		trace << "ARP in "<< endl;
		trace << "Sender mac: " << aHeader->senderEthAddress << endl;
		trace << "Sender IP: " << aHeader->senderIPAddress << endl;
		trace << "Target mac: " << aHeader->targetEthAddress << endl;
		trace << "Target IP: " << aHeader->targetIPAddress << endl;

		replyHeader->op = HILO(2);
		replyHeader->targetEthAddress = aHeader->senderEthAddress;
		replyHeader->targetIPAddress = aHeader->senderIPAddress;
		replyHeader->senderIPAddress = IP::instance().myAddress();
		replyHeader->senderEthAddress = Ethernet::instance().myAddress();

		trace << "ARP out "<< endl;
		trace << "Sender mac: " << replyHeader->senderEthAddress << endl;
		trace << "Sender IP: " << replyHeader->senderIPAddress << endl;
		trace << "Target mac: " << replyHeader->targetEthAddress << endl;
		trace << "Target IP: " << replyHeader->targetIPAddress << endl;

  	this->answer(aReply,myLength);

	}
	delete aHeader; 


}

void
ARPInPacket::answer(byte* theData, udword theLength)
{

	myFrame->answer(theData, theLength);
}

uword
ARPInPacket::headerOffset()
{
   return myFrame->headerOffset();
}
