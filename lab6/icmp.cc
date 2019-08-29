#include "compiler.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
extern "C"
{
#include "system.h"
}

#include "iostream.hh"
#include "icmp.hh"
#include "ip.hh"

//#define D_LLC
#ifdef D_LLC
#define trace cout
#else
#define trace if(false) cout
#endif
#define trace if(false) cout

ICMPInPacket::ICMPInPacket(byte* theData, udword theLength, InPacket* theFrame)  :
	InPacket(theData, theLength, theFrame)
{
}

void
ICMPInPacket::decode()
{
		ICMPHeader *aHeader = (ICMPHeader*) myData;
		trace << "Value icmp type: " <<hex << int(aHeader->type) << endl;
		if(aHeader->type == 8){
						// create a resonse copied from LLC
						uword hoffs = myFrame->headerOffset(); // MyFrame = IP => hoffs = ethernet + IP length
						byte* temp = new byte[myLength + hoffs + ICMPInPacket::icmpHeaderLen];
						byte* aReply = temp + hoffs; // starts at the length.
						memcpy(aReply, myData, myLength);

						ICMPHeader *replyHeader = (ICMPHeader*) aReply;
						// Change to reply
			  	  replyHeader->type = 0;

						// Checksum stuff.
						uword oldSum = aHeader->checksum;
					  uword newSum = oldSum + 0x8;
						replyHeader->checksum = newSum;
						trace << "new checksum in icmp: " <<hex << int(replyHeader->checksum) << endl;
					  this->answer(aReply, myLength + hoffs);
		}
}

void
ICMPInPacket::answer(byte* theData, udword theLength)
{
		myFrame->answer(theData, theLength);
}

uword ICMPInPacket::headerOffset()
{
		return myFrame->headerOffset() + icmpEchoHeaderLen;
}
