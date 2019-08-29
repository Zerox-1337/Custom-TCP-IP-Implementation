/*!***************************************************************************
*!
*! FILE NAME  : http.cc
*!
*! DESCRIPTION: HTTP, Hyper text transfer protocol.
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
}

#include "iostream.hh"
#include "tcpsocket.hh"
#include "http.hh"
#include "fs.hh"
#include "tcp.hh"

//#define D_HTTP
#ifdef D_HTTP
#define trace cout
#else
#define trace if(false) cout
#endif

/****************** HTTPServer DEFINITION SECTION ***************************/
HTTPServer::HTTPServer(TCPSocket* theSocket):
mySocket(theSocket){
  reply404 =
    "HTTP/1.0 404 Not found\r\n "
    "Content-type: text/html\r\n "
    "\r\n "
    "<html><head><title>File not found</title></head>"
    "<body><h1>404 Not found</h1></body></html>";
  statusReplyOk = "HTTP/1.0 200 OK\r\n";
  contentReplyText = "Content-type: text/html\r\n\r\n";
  contentReplyJpeg = "Content-type: image/jpeg\r\n\r\n";
  contentReplyGif = "Content-type: image/gif\r\n\r\n";
  replyUnAut =
    "HTTP/1.0 401 Unauthorized\r\n"
    "Content-Type: text/html\r\n"
    "WWW-Authenticate: Basic realm=\"private\"\r\n"
    "\r\n"
    "<html><head><title>401 Unauthorized</title></head>\r\n"
    "<body><h1>401 Unauthorized</h1></body></html>";
  replyDynamic =
    "HTTP/1.0 200 OK\r\n"
    "Content-type: text/html\r\n"
    "\r\n"
    "<html><head><title>Accepted</title></head>"
    "<body><h1>The file dynamic.htm was updated successfully.</h1></body></html>";
}



HTTPServer::~HTTPServer() {
  delete reply404;
  delete statusReplyOk;
  delete contentReplyGif;
  delete contentReplyText;
  delete contentReplyJpeg;
  delete replyDynamic;
  delete replyUnAut;
}

void
HTTPServer::doit() {
  trace << "HTTPServer::doit" << endl;
  udword aLength;
  byte* aData;
  aData = mySocket->Read(aLength); // Returns the read data and sets aLength
  if (aLength > 0) {
  if (strncmp((char*)aData, "GET", 3) == 0) {
    handleGet(aData, aLength);
  } else if (strncmp((char*)aData, "POST", 4) == 0) {
    udword moreLength = 0;
    byte* moreData = mySocket->Read(moreLength);
    byte* totalHeader = new byte[aLength+moreLength];
    memcpy(totalHeader, aData, aLength);
    memcpy(totalHeader+aLength, moreData, moreLength);
    delete [] aData;
    delete [] moreData;
    handlePost(totalHeader, aLength + moreLength);
}
delete aData;

//trace << "Closed Socket " << mySocket->myConnection->hisPort << endl;
mySocket->Close();
}
}

// Post is for dynamaic web.
/*
 The updated content of the file /dynamic/dynamic.htm is transferred to the server with a POST request
 when the form in the resource /private/private.htm is submitted. The file content in the POST request
 is URL encoded and must be decoded with the provided method HTTPServer::decodeForm before the file
 is updated
*/
void
HTTPServer::handlePost(byte* aData, udword aLength){
  char* filePath = findPathName((char*)aData);
  trace << "handling POST: " << filePath << endl;
  char* pathWithFile = "";
  if (filePath != NULL) {
    //parse filename/type
    char* firstPos = strchr((char*)aData, ' ');   // First space on line
    firstPos++;                            // Pointer to first /
    char* lastPos = strchr(firstPos, ' '); // Last space on line
    pathWithFile = extractString((char*)(firstPos+1), lastPos-firstPos);
    char* fileName = strrchr(pathWithFile, '/');
    fileName += 1; //skip '/'
    //trace << "fileName: " << fileName << " port: " << mySocket->myConnection->hisPort << endl;
    char* fileType = strrchr(fileName, '.');
    fileType += 1; // skip '.'
    //trace << "fileType: " << fileType << " port: "<< mySocket->myConnection->hisPort << endl;

    if (strncmp(fileName, "private", 7) == 0) {
      udword thisContentLength = contentLength((char*)aData, aLength);

      char* fileStart = (char*)aData;
      trace << "fileStart: " << fileStart << endl;
      fileStart = skipHeader(fileStart);
      trace << "After Skip header: " <<fileStart << endl;
      //char* recFileNameEnd = strchr(fileStart, '=');
      trace <<  "skipped header" << endl;

      udword totalReadLength = aLength - ((udword)fileStart - (udword)aData);
      trace << "Total Data Read: " << totalReadLength << endl;
      byte* allData = new byte[thisContentLength + 1];
      memcpy(allData, fileStart, totalReadLength);
      delete [] aData;

      while(totalReadLength < thisContentLength){
        trace << "Receive inside while" << endl;
        byte* newData= mySocket->Read(aLength);
        totalReadLength += aLength;
        trace << "Read in while: " << aLength << " bytes, total so far: " << totalReadLength << endl;
        memcpy(allData + totalReadLength - aLength, newData, aLength);
        delete newData;
      }
      trace << "After while, all data received" << endl;
      allData[thisContentLength] = '\0';
      char* decodedFile = decodeForm((char*)allData);

      //char* recFileName = extractString((char*)allData, (udword)recFileNameEnd - (udword)fileStart);
      //trace << "POST receive all file we happy" << " port: "<< mySocket->myConnection->hisPort << endl;
      trace << decodedFile << endl;

      delete [] allData;
      mySocket->Write((byte*)decodedFile, strlen(decodedFile));
      trace << strlen(decodedFile) << endl;
      char* dynName = "dynamic.htm";
      FileSystem::instance().writeFile(dynName, (byte*)decodedFile, strlen(decodedFile));
      delete dynName;
      delete decodedFile;
    }
  }
  delete filePath;
  delete pathWithFile;
}

// establish connection to start page, image and such.
void
HTTPServer::handleGet(byte* aData, udword aLength){
  char* filePath = findPathName((char*)aData);
  trace << "filePath: " << filePath << endl;
  byte* replyFile = 0;
  udword replyLength;
  char* pathWithFile;
  char* contentStatus = NULL;
  char* contentType = NULL;
  if (filePath == NULL) { // No file thus we go to index.
    //root
    char* firstPos = strchr((char*)aData, ' ');   // index of First space on line
    firstPos++;                            // Pointer to first /
    char* lastPos = strchr(firstPos, ' '); // Last space on line
    pathWithFile = extractString((char*)(firstPos+1), lastPos-firstPos); // Gets the string between two spaces
    char* fileName = (char*)(strrchr(pathWithFile ,'/') + 1); // Last occurance of "/" index, which will get the name of file.
    if (*fileName == '\0') { // Check if null character. This means we have no file we entered for example google.se we get index file
      char* indexFile = "index.htm"; // Reads file.
      replyFile = FileSystem::instance().readFile(filePath, indexFile, replyLength);
      contentStatus = statusReplyOk; // Messages from lab manual defined in constructor. Something is okay
      contentType = contentReplyText; // Messages from lab manual defined in constructor. Is it text, JPG, gif or whatever. What content type it is
      trace << "sending index to client" << endl;
      delete fileName;
    }
  } else { // We check which file and such. like JPG, GIF
    //parse filename/type
    char* firstPos = strchr((char*)aData, ' ');   // First space on line
    firstPos++;                            // Pointer to first /
    char* lastPos = strchr(firstPos, ' '); // Last space on line
    pathWithFile = extractString((char*)(firstPos+1), lastPos-firstPos);
    char* fileName = strrchr(pathWithFile, '/');
    fileName += 1; //skip '/'
    //trace << "fileName: " << fileName << " port: " << mySocket->myConnection->hisPort << endl;
    char* fileType = strrchr(fileName, '.');
    fileType += 1; // skip '.'
    //trace << "fileType: " << fileType << " port: "<< mySocket->myConnection->hisPort << endl;
    if (strncmp(fileType, "jpg", 3) == 0) {
      contentStatus = statusReplyOk;
      contentType = contentReplyJpeg; // It's content is JPG message (check contructor)
      //trace << "found jpg request" << " port: "<< mySocket->myConnection->hisPort << endl;
    } else if (strncmp(fileType, "gif", 3) == 0) {
      contentStatus = statusReplyOk;
      contentType = contentReplyGif;
      //trace << "found gif request" << " port: "<< mySocket->myConnection->hisPort << endl;
    } else if (strncmp(fileType, "htm", 3) == 0) {
      contentStatus = statusReplyOk;
      contentType = contentReplyText;
      //trace << "found htm request" << " port: "<< mySocket->myConnection->hisPort << endl;
    }
    replyFile = FileSystem::instance().readFile(filePath, fileName, replyLength); // Sends back the page that is requested
  }

  if (strncmp(filePath, "private", 7) == 0) { // Authenication part. if has private in file path
    if(correctAuth((char*)aData) && replyFile != 0){ // Check if file exist and if we are authorized to access it (correctAuth)
      mySocket->Write((byte*)contentStatus, strlen(contentStatus));
      mySocket->Write((byte*)contentType, strlen(contentType));
      mySocket->Write(replyFile, replyLength);
    } else if(replyFile == 0){ // File we enter in HTML doesn't exist.
      mySocket->Write((byte*)reply404, strlen(reply404));
    } else { // We are not authorized to access file.
      mySocket->Write((byte*)replyUnAut, strlen(replyUnAut)); // Not authorized message sent back.
    }

  } else if(replyFile == 0){ // File we enter in HTML doesn't exist.
    //404
    //trace << "404 len: " << strlen(reply404) << endl;
    mySocket->Write((byte*)reply404, strlen(reply404));
  } else {
    //trace << "Before replyFile != 0 port: "<< mySocket->myConnection->hisPort << endl;
    mySocket->Write((byte*)contentStatus, strlen(contentStatus)); // Same as for when private and we authoize.
    mySocket->Write((byte*)contentType, strlen(contentType));
    mySocket->Write(replyFile, replyLength);
    //trace << "After replyFile != 0 port: "<< mySocket->myConnection->hisPort << endl;
  }
  delete filePath;
  delete pathWithFile;
}



char*
HTTPServer::skipHeader(char* aData){
  bool headerDone = false;
  char* line = strstr((char*)aData, "\r\n");
  line += 2;
  while (!headerDone){
    line = (strstr((char*)line, "\r\n") + 2);
    if(strncmp(line, line - 2, 2) == 0){
      headerDone = true;
    }
  }
  return line + 2;
}


bool
HTTPServer::correctAuth(char* aData){
  char* password = NULL;
      char* line = strstr((char*)aData, "\r\n");
      line += 2;

      bool headerDone = false;
      while (!headerDone){
        if(strncmp(line, "Authorization: Basic", 19) == 0){
          line += 16;
          char* firstPos = strchr((char*)line, ' ');   // First space on line
          firstPos++;                            // Pointer to first /
          char* lastPos = strchr(firstPos, '\r'); // Last space on line
          password = extractString((char*)(firstPos), lastPos-firstPos);
          *(strstr(password, "\r\n")) = '\0';


        }
        line = (strstr((char*)line, "\r\n") + 2);
        if(strncmp(line, line - 2, 2) == 0){
          headerDone = true;
        }
      }

      if(password != NULL){
        char* decodedPass = decodeBase64(password);
        if(strncmp("jm:123", decodedPass, 6) == 0){
          trace << "accepted pass" << endl;
          delete password;
          delete decodedPass;
          return true;
        }
        delete decodedPass;
      }
      delete password;
      return false;
}

char*
HTTPServer::findPathName(char* str) // From lab manual.
{
  char* firstPos = strchr(str, ' ');     // First space on line
  firstPos++;                            // Pointer to first /
  char* lastPos = strchr(firstPos, ' '); // Last space on line
  char* thePath = 0;                     // Result path
  if ((lastPos - firstPos) == 1)
  {
    // Is / only
    thePath = 0;                         // Return NULL
  }
  else
  {
    // Is an absolute path. Skip first /.
    thePath = extractString((char*)(firstPos+1),
                            lastPos-firstPos);
    if ((lastPos = strrchr(thePath, '/')) != 0)
    {
      // Found a path. Insert -1 as terminator.
      *lastPos = '\xff';
      *(lastPos+1) = '\0';
      while ((firstPos = strchr(thePath, '/')) != 0)
      {
        // Insert -1 as separator.
        *firstPos = '\xff';
      }
    }
    else
    {
      // Is /index.html
      delete thePath; thePath = 0; // Return NULL
    }
  }
  return thePath;
}



//----------------------------------------------------------------------------
//
// Allocates a new null terminated string containing a copy of the data at
// 'thePosition', 'theLength' characters long. The string must be deleted by
// the caller.
//
char*
HTTPServer::extractString(char* thePosition, udword theLength)
{
  char* aString = new char[theLength + 1];
  strncpy(aString, thePosition, theLength);
  aString[theLength] = '\0';
  return aString;
}

//----------------------------------------------------------------------------
//
// Will look for the 'Content-Length' field in the request header and convert
// the length to a udword
// theData is a pointer to the request. theLength is the total length of the
// request.
//
udword
HTTPServer::contentLength(char* theData, udword theLength)
{
  udword index = 0;
  bool   lenFound = false;
  const char* aSearchString = "Content-Length: ";
  while ((index++ < theLength) && !lenFound)
  {
    lenFound = (strncmp(theData + index,
                        aSearchString,
                        strlen(aSearchString)) == 0);
  }
  if (!lenFound)
  {
    return 0;
  }
  trace << "Found Content-Length!" << endl;
  index += strlen(aSearchString) - 1;
  char* lenStart = theData + index;
  char* lenEnd = strchr(theData + index, '\r');
  char* lenString = this->extractString(lenStart, lenEnd - lenStart);
  udword contLen = atoi(lenString);
  trace << "lenString: " << lenString << " is len: " << contLen << endl;
  delete [] lenString;
  return contLen;
}




//----------------------------------------------------------------------------
//
// Decode user and password for basic authentication.
// returns a decoded string that must be deleted by the caller.
//
char*
HTTPServer::decodeBase64(char* theEncodedString)
{
  static const char* someValidCharacters =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

  int aCharsToDecode;
  int k = 0;
  char  aTmpStorage[4];
  int aValue;
  char* aResult = new char[80];

  // Original code by JH, found on the net years later (!).
  // Modify on your own risk.

  for (unsigned int i = 0; i < strlen(theEncodedString); i += 4)
  {
    aValue = 0;
    aCharsToDecode = 3;
    if (theEncodedString[i+2] == '=')
    {
      aCharsToDecode = 1;
    }
    else if (theEncodedString[i+3] == '=')
    {
      aCharsToDecode = 2;
    }

    for (int j = 0; j <= aCharsToDecode; j++)
    {
      int aDecodedValue;
      aDecodedValue = strchr(someValidCharacters,theEncodedString[i+j])
        - someValidCharacters;
      aDecodedValue <<= ((3-j)*6);
      aValue += aDecodedValue;
    }
    for (int jj = 2; jj >= 0; jj--)
    {
      aTmpStorage[jj] = aValue & 255;
      aValue >>= 8;
    }
    aResult[k++] = aTmpStorage[0];
    aResult[k++] = aTmpStorage[1];
    aResult[k++] = aTmpStorage[2];
  }
  aResult[k] = 0; // zero terminate string

  return aResult;
}

//------------------------------------------------------------------------
//
// Decode the URL encoded data submitted in a POST.
//
char*
HTTPServer::decodeForm(char* theEncodedForm)
{
  char* anEncodedFile = strchr(theEncodedForm,'=');
  anEncodedFile++;
  char* aForm = new char[strlen(anEncodedFile) * 2];
  // Serious overkill, but what the heck, we've got plenty of memory here!
  udword aSourceIndex = 0;
  udword aDestIndex = 0;

  while (aSourceIndex < strlen(anEncodedFile))
  {
    char aChar = *(anEncodedFile + aSourceIndex++);
    switch (aChar)
    {
     case '&':
       *(aForm + aDestIndex++) = '\r';
       *(aForm + aDestIndex++) = '\n';
       break;
     case '+':
       *(aForm + aDestIndex++) = ' ';
       break;
     case '%':
       char aTemp[5];
       aTemp[0] = '0';
       aTemp[1] = 'x';
       aTemp[2] = *(anEncodedFile + aSourceIndex++);
       aTemp[3] = *(anEncodedFile + aSourceIndex++);
       aTemp[4] = '\0';
       udword anUdword;
       anUdword = strtoul((char*)&aTemp,0,0);
       *(aForm + aDestIndex++) = (char)anUdword;
       break;
     default:
       *(aForm + aDestIndex++) = aChar;
       break;
    }
  }
  *(aForm + aDestIndex++) = '\0';
  return aForm;
}

/************** END OF FILE http.cc *************************************/
