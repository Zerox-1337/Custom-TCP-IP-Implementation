/*!***************************************************************************
*!
*! FILE NAME  : tcpsocket.hh
*!
*! DESCRIPTION: TCPSocket is the TCP protocols stacks interface to the
*!              application.
*!
*!***************************************************************************/

#ifndef http_hh
#define http_hh

/****************** INCLUDE FILES SECTION ***********************************/

#include "job.hh"

/****************** CLASS DEFINITION SECTION ********************************/

/*****************************************************************************
*%
*% CLASS NAME   : TCPSocket
*%
*% BASE CLASSES : None
*%
*% CLASS TYPE   : Singleton
*%
*% DESCRIPTION  : Handles the interace beetwen TCP and the application.
*%
*% SUBCLASSING  : None.
*%
*%***************************************************************************/
class HTTPServer : public Job
{
 public:
  HTTPServer(TCPSocket* theSocket);
  // Constructor. The socket is created by class TCP when a connection is
  // established. create the semaphores
  ~HTTPServer();
  // Destructor. destroy the semaphores.

  //
  // Interface to application
  //

char* extractString(char* thePosition, udword theLength); // Got already

udword contentLength(char* theData, udword theLength); // Got already

char* decodeBase64(char* theEncodedString); // Got already

char* decodeForm(char* theEncodedForm); // Got already

char* findPathName(char* str); // copied from lab manual

void doit();

bool correctAuth(char* header);

char* skipHeader(char* data);

void handleGet(byte* aData, udword aLength);

void handlePost(byte* aData, udword aLength);


 private:
  TCPSocket* mySocket;
   char* reply404;
   char* statusReplyOk;
   char* contentReplyText;
   char* contentReplyJpeg;
   char* contentReplyGif;
   char* replyUnAut;
   char* replyDynamic;
};



#endif
/****************** END OF FILE tcpsocket.hh *********************************/
