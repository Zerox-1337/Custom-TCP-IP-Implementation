/*!***************************************************************************
*!
*! FILE NAME  : FrontPanel.cc
*!
*! DESCRIPTION: Handles the LED:s
*!
*!***************************************************************************/




/****************** INCLUDE FILES SECTION ***********************************/

#include "compiler.h"

#include "iostream.hh"
#include "frontpanel.hh"

//#define D_FP
#ifdef D_FP
#define trace cout
#else
#define trace if(false) cout
#endif

byte LED::writeOutRegisterShadow = 0x38; // 38 = bit 3-5 (counting from 0) is set to 1 which means that the leds are off. Registers can not be read from, so the value has to be stored here.

/******************  LED DEFINITION SECTION ***************************/

// Definitions in .hh
// Implementation in .cc
//----------------------------------------------------------------------------
//

LED::LED(byte theLedNumber)

{ // Lednumber = 1-3. Network = bit 3 (count from 0), cd = bit 5, Status = bit 4.
	myLedBit = 4 << theLedNumber; // 1 at the bit position for the led.
}

void
LED::on() {
	  /* convert LED number to bit weight */
	iAmOn = true; // Used to for toggle to know the state of the led.
	*(VOLATILE byte*)0x80000000 = writeOutRegisterShadow &= ~myLedBit; // Sets the register bit to 0 = on
}

void
LED::off() {
	iAmOn = false;
	*(VOLATILE byte*)0x80000000 = writeOutRegisterShadow |= myLedBit; // Sets the register bit to 1 = off
}

void
LED::toggle() {
	if (iAmOn == false)
	{
		on();
	}
	else
	{
		off();
	}

}

//                      NETWORK LED Timed
// Blinks once after start() is called
NetworkLEDTimer::NetworkLEDTimer(Duration blinkTime)
{
	myBlinkTime = blinkTime; // The time the led blinks
	//start();
}

void
NetworkLEDTimer::start()
{

	this->timeOutAfter(myBlinkTime); // Starts the timer
}
void
NetworkLEDTimer::timeOut() // Called when timer is finished
{
	//FrontPanel::instance().myNetworkLED.off();
	FrontPanel::instance().notifyLedEvent(FrontPanel::networkLedId);    // Notifies the frontpanel that the network led timer has timedout and that frontpanel should turn off led.
}



//                       CD LED PeriodicTimed
CDLEDTimer::CDLEDTimer(Duration blinkPeriod)
{
	//FrontPanel::instance().myCDLED.on();startPeriodicTimer()
	this->timerInterval(blinkPeriod);
	this->startPeriodicTimer(); // Starts a periodic timer that calls timerNotify multiple times.
}


void
CDLEDTimer::timerNotify()
{
	//FrontPanel::instance().myCDLED.toggle();
	FrontPanel::instance().notifyLedEvent(FrontPanel::cdLedId); // Notifies the frontpanel that the status led timer has timedout and that frontpanel should toggle led.
}


//                    STATUS LED PeriodicTimed
StatusLEDTimer::StatusLEDTimer(Duration blinkPeriod)
{
	//FrontPanel::instance().myStatusLED.on();
	this->timerInterval(blinkPeriod);
	this->startPeriodicTimer(); // Starts a periodic timer that calls timerNotify multiple times.
}


void
StatusLEDTimer::timerNotify() // Called periodically.
{
	//FrontPanel::instance().myStatusLED.toggle();
	FrontPanel::instance().notifyLedEvent(FrontPanel::statusLedId); // Notifies the frontpanel that the status led timer has timedout and that frontpanel should toggle led.
}

/****************** FrontPanel DEFINITION SECTION ***************************/

// Definitions in .hh
// Implementation in .cc
//----------------------------------------------------------------------------
//

FrontPanel::FrontPanel() :
	Job(), // Normal none pointers that are declared/defined in the hh file are initialized this way. For pointers check the first lines of doit().
	mySemaphore(Semaphore::createQueueSemaphore("FP", 0)),
	myNetworkLED(networkLedId), // The LED myNetworkLED is initiated
	myCDLED(cdLedId),
	myStatusLED(statusLedId)

{

	//cout << "FrontPanel created." << endl;
	Job::schedule(this);
}

FrontPanel& // Address/reference to instace.
FrontPanel::instance() // Creates singleton class only one instance.
{
	static FrontPanel myInstance;
	return myInstance;
}

// Main thread loop of FrontPanel. Initializes the led timers and goes into
// a perptual loop where it awaits the semaphore. When it wakes it checks
// the event flags to see which leds to manipulate and manipulates them.

void
FrontPanel::doit()
{
	myNetworkLEDTimer = new NetworkLEDTimer(2); // Intializes the timers. We don't use stars like in the code in this case because they're pointers in .hh
	myStatusLEDTimer = new StatusLEDTimer(Clock::seconds*2);
	myCDLEDTimer = new CDLEDTimer(Clock::seconds*5);

	//packetReceived(); // Simulates a packet being received to show a short blink of network led. REMOVED IN LAB 2.
	while (true)
	{ // Semaphore = used for controlling the access to a common resource by multiple processes. For example: Two threads are used to move an object, semaphore makes sure they don't move it at the same time.
		mySemaphore->wait(); // Waits for one call of notifyLedEvent (which sets the bool values like: netLedEvent to true, when it's called).
		if (netLedEvent == true)
		{
			myNetworkLED.off();
			//cout << "Network LED flash" << endl;
			netLedEvent = false;

		}
		if (cdLedEvent == true)
		{
			myCDLED.toggle();
			//cout << "CD LED flash" << endl;
			cdLedEvent = false;
			//cout << "Core " << ax_coreleft_total() << endl;
		}
		if (statusLedEvent == true)
		{
			myStatusLED.toggle();
			//cout << "Status LED flash" << endl;
			cout << "Core " << ax_coreleft_total() << endl;
			statusLedEvent = false;
		}

	}

}

// turn Network led on and start network led timer
void
FrontPanel::packetReceived()
{
	myNetworkLED.on(); // Turns on network led. Has to be here since it's private.
	myNetworkLEDTimer->start(); // Starts the led and timer. Uses -> to call object/instance method/function.
}

// Called from the timers to notify that a timer has expired.
// Sets an event flag and signals the semaphore.
void
FrontPanel::notifyLedEvent(uword theLedId)
{

	if (theLedId == 1) // Network led.
	{
		netLedEvent = true; // Event flag - The timer for network led was the one who notified us. Bools is used in doit() to determine what to do.
	}
	else if (theLedId == 2) // Status led
	{
		cdLedEvent = true; // Event flag - The timer for cd led was the one who notified us.
	}
	else if (theLedId == 3) // Cd Led
	{
		statusLedEvent = true; // Event flag - The timer for status led was the one who notified us.
	}
	mySemaphore->signal(); // This signals the semaphore to give control to manipulating the leds. (the code runs past wait()).
}


//----------------------------------------------------------------------------

/****************** END OF FILE FrontPanel.cc ********************************/
