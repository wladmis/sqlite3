/*
** A small, simple HTTP server.
**
** Features:
**
**     * Launched from inetd
**     * One process per request
**     * Deliver static content or run CGI
**     * Virtual sites based on the "Host:" property of the HTTP header
**     * Very small code base (1 file) to facility security auditing
**     * Simple setup - no configuration files to mess with.
** 
** This file implements a small and simple but secure and effective web
** server.  There are no frills.  Anything that could be reasonably
** omitted has been.
**
** Setup rules:
**
**    (1) Launch as root from inetd like this:
**
**            httpd -logfile logfile -root /home/www -user nobody
**
**        It will automatically chroot to /home/www and become user nobody.
**        The logfile name should be relative to the chroot jail.
**
**    (2) Directories of the form "*.website" (ex: www_hwaci_com.website)
**        contain content.  The directory is chosen based on HOST.  If no
**        HOST or the host directory is not found, "default.website" is used.
**
**    (3) Any file or directory whose name begins with "." or "-" is ignored.
**
**    (4) Characters other than a-zA-Z0-9_.,*~/ in the filename are translated
**        into _.  This is a defense against cross-site scripting attacks and
**        other mischief.
**
**    (5) Executable files are run as CGI.  All other files are delivered
**        as is.
*/
#include <stdio.h>
#include <ctype.h>
#include <syslog.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <pwd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <crypt.h>
#include <stdarg.h>
#include <time.h>
#include <sys/times.h>
#include <sys/sendfile.h>

/*
** Configure the server by setting the following macros and recompiling.
*/
#ifndef DEFAULT_PORT
#define DEFAULT_PORT "80"
#endif
#ifndef MAX_CONTENT_LENGTH
#define MAX_CONTENT_LENGTH 5000000
#endif


/*
** The error number from system calls.
*/
extern int errno;

/*
** We record most of the state information as global variables.  This
** saves having to pass information to subroutines as parameters, and
** makes the executable smaller...
*/
static char *zRoot = 0;          /* Root directory of the website */
static char *zTmpNam = 0;        /* Name of a temporary file */
static char zTmpNamBuf[500];     /* Space to hold the temporary filename */
static char *zProtocol = 0;      /* The protocol being using by the browser */
static char *zMethod = 0;        /* The method.  Must be GET */
static char *zScript = 0;        /* The object to retrieve */
static char *zRealScript = 0;    /* The object to retrieve.  Same as zScript
                                 ** except might have "/index.html" appended */
static char *zHome = 0;          /* The directory containing content */
static char *zQueryString = 0;   /* The query string on the end of the name */
static char *zFile = 0;          /* The filename of the object to retrieve */
static int lenFile = 0;          /* Length of the zFile name */
static char *zDir = 0;           /* Name of the directory holding zFile */
static char *zPathInfo = 0;      /* Part of the pathname past the file */
static char *zAgent = 0;         /* What type if browser is making this query */
static char *zServerName = 0;    /* The name after the http:// */
static char *zServerPort = 0;    /* The port number */
static char *zCookie = 0;        /* Cookies reported with the request */
static char *zHttpHost = 0;      /* Name according to the web browser */
static char *zRealPort = 0;      /* The real TCP port when running as daemon */
static char *zRemoteAddr = 0;    /* IP address of the request */
static char *zReferer = 0;       /* Name of the page that refered to us */
static char *zAccept = 0;        /* What formats will be accepted */
static char *zContentLength = 0; /* Content length reported in the header */
static char *zContentType = 0;   /* Content type reported in the header */
static char *zQuerySuffix = 0;   /* The part of the URL after the first ? */
static int nIn = 0;              /* Number of bytes of input */
static int nOut = 0;             /* Number of bytes of output */
static char zReplyStatus[4];     /* Reply status code */
static int statusSent = 0;       /* True after status line is sent */
static char *zLogFile = 0;       /* Log to this file */
static int debugFlag = 0;        /* True if being debugged */
static time_t beginTime;         /* Time when this process starts */
static int closeConnection = 0;  /* True to send Connection: close in reply */
static int nRequest = 0;         /* Number of requests processed */
static int omitLog = 0;          /* Do not make logfile entries if true */

/*
** Change every space or unprintable character in the zAgent[] string
** into an _.
**
** If the user agent string contains certain prohibited string, then
** exit immediately.
*/
static void FixupUserAgent(void){
  int i;
  if( zAgent==0 || zAgent[0]==0 ) zAgent = "*";
  for(i=0; zAgent[i]; i++){
    int c = zAgent[i];
    if( c<'!' || c>'~'  ){ zAgent[i] = '_'; }
  }
  if( strncmp(zAgent,"msnbot",6)==0 ){
    exit(0);
  }
  for(i=0; zAgent[i]; i++){
    if( zAgent[i]=='W' && strncmp(&zAgent[i],"Windows_9",9)==0 ){
      exit(0);
    }
  }
}

/*
** Make an entry in the log file.  If the HTTP connection should be
** closed, then terminate this process.  Otherwise return.
*/
static void MakeLogEntry(int a){
  FILE *log;
  if( zTmpNam ){
    unlink(zTmpNam);
  }
  if( zLogFile && !omitLog ){
    time_t now;
    struct tm *pTm;
    struct tms sTms;
    double rScale;
    int i;
    char zDate[200];

    if( zScript==0 || zScript[0]==0 ) zScript = "*";
    if( zRemoteAddr==0 || zRemoteAddr[0]==0 ) zRemoteAddr = "*";
    if( zHttpHost==0 || zHttpHost[0]==0 ) zHttpHost = "*";
    if( zReferer==0 || zReferer[0]==0 ) zReferer = "*";
    for(i=0; zReferer[i]; i++){ 
      if( isspace(zReferer[i]) ){ zReferer = "*"; break; }
    }
    if( zAgent==0 || zAgent[0]==0 ) zAgent = "*";
    time(&now);
    pTm = localtime(&now);
    strftime(zDate, sizeof(zDate), "%Y-%m-%d %H:%M:%S", pTm);
    times(&sTms);
    rScale = 1.0/(double)sysconf(_SC_CLK_TCK);
    chdir(zRoot[0] ? zRoot : "/");
    if( (log = fopen(zLogFile,"a"))!=0 ){
      fprintf(log, "%s %s http://%s%s %s %s %d %d %g %g %g %g %d %d %s\n", 
          zDate, zRemoteAddr, zHttpHost, zScript, zReferer,
          zReplyStatus, nIn, nOut,
          rScale*sTms.tms_utime,
          rScale*sTms.tms_stime,
          rScale*sTms.tms_cutime,
          rScale*sTms.tms_cstime,
          now - beginTime,
          nRequest, zAgent
      );
      fclose(log);
      nIn = nOut = 0;
    }
  }
  if( closeConnection ){
    exit(a);
  }
  statusSent = 0;
}

/*
** Allocate memory safely
*/
static char *SafeMalloc( int size ){
  char *p;

  p = (char*)malloc(size);
  if( p==0 ){
    strcpy(zReplyStatus, "998");
    MakeLogEntry(1);
    exit(1);
  }
  return p;
}

/*
** Set the value of environment variable zVar to zValue.
*/
static void SetEnv(const char *zVar, const char *zValue){
  char *z;
  int len;
  if( zValue==0 ) zValue="";
  len = strlen(zVar) + strlen(zValue) + 2;
  z = SafeMalloc(len);
  sprintf(z,"%s=%s",zVar,zValue);
  putenv(z);
}

/*
** Remove the first space-delimited token from a string and return
** a pointer to it.  Add a NULL to the string to terminate the token.
** Make *zLeftOver point to the start of the next token.
*/
static char *GetFirstElement(char *zInput, char **zLeftOver){
  char *zResult = 0;
  if( zInput==0 ){
    if( zLeftOver ) *zLeftOver = 0;
    return 0;
  }
  while( isspace(*zInput) ){ zInput++; }
  zResult = zInput;
  while( *zInput && !isspace(*zInput) ){ zInput++; }
  if( *zInput ){
    *zInput = 0;
    zInput++;
    while( isspace(*zInput) ){ zInput++; }
  }
  if( zLeftOver ){ *zLeftOver = zInput; }
  return zResult;
}

/*
** Make a copy of a string into memory obtained from malloc.
*/
static char *StrDup(const char *zSrc){
  char *zDest;
  int size;

  if( zSrc==0 ) return 0;
  size = strlen(zSrc) + 1;
  zDest = (char*)SafeMalloc( size );
  strcpy(zDest,zSrc);
  return zDest;
}
static char *StrAppend(char *zPrior, const char *zSep, const char *zSrc){
  char *zDest;
  int size;
  int n1, n2;

  if( zSrc==0 ) return 0;
  if( zPrior==0 ) return StrDup(zSrc);
  size = (n1=strlen(zSrc)) + (n2=strlen(zSep)) + strlen(zPrior) + 1;
  zDest = (char*)SafeMalloc( size );
  strcpy(zDest,zPrior);
  free(zPrior);
  strcpy(&zDest[n1],zSep);
  strcpy(&zDest[n1+n2],zSrc);
  return zDest;
}

/*
** Break a line at the first \n or \r character seen.
*/
static void RemoveNewline(char *z){
  if( z==0 ) return;
  while( *z && *z!='\n' && *z!='\r' ){ z++; }
  *z = 0;
}

/*
** Print a date tag in the header.  The name of the tag is zTag.
** The date is determined from the unix timestamp given.
*/
static int DateTag(const char *zTag, time_t t){
  struct tm *tm;
  char zDate[100];
  tm = gmtime(&t);
  strftime(zDate, sizeof(zDate), "%a, %d  %b %Y %H:%M:%S %z", tm);
  return printf("%s: %s\r\n", zTag, zDate);
}

/*
** Print the first line of a response followed by the server type.
*/
static void StartResponse(const char *zResultCode){
  time_t now;
  time(&now);
  if( statusSent ) return;
  nOut += printf("%s %s\r\n", zProtocol, zResultCode);
  strncpy(zReplyStatus, zResultCode, 3);
  zReplyStatus[3] = 0;
  if( zReplyStatus[0]>='4' ){
    closeConnection = 1;
  }
  if( closeConnection ){
    nOut += printf("Connection: close\r\n");
  }else{
    nOut += printf("Connection: keep-alive\r\n");
  }
  nOut += DateTag("Date", now);
  statusSent = 1;
}

/*
** Tell the client that there is no such document
*/
static void NotFound(int lineno){
  StartResponse("404 Not Found");
  nOut += printf(
    "Content-type: text/html\r\n"
    "\r\n"
    "<head><title lineno=\"%d\">Not Found</title></head>\n"
    "<body><h1>Document Not Found</h1>\n"
    "The document %s is not avaivable on this server\n"
    "</body>\n", lineno, zScript);
  MakeLogEntry(0);
  exit(0);
}

/*
** Tell the client that there is an error in the script.
*/
static void CgiError(void){
  StartResponse("500 Error");
  nOut += printf(
    "Content-type: text/html\r\n"
    "\r\n"
    "<head><title>CGI Program Error</title></head>\n"
    "<body><h1>CGI Program Error</h1>\n"
    "The CGI program %s generated an error\n"
    "</body>\n", zScript);
  MakeLogEntry(0);
  exit(0);
}

/*
** This is called if we timeout.
*/
static void Timeout(int NotUsed){
  if( !debugFlag ){
    strcpy(zReplyStatus, "999");
    MakeLogEntry(0);
    exit(0);
  }
}

/*
** Tell the client that there is an error in the script.
*/
static void CgiScriptWritable(void){
  StartResponse("500 CGI Configuration Error");
  nOut += printf(
    "Content-type: text/html\r\n"
    "\r\n"
    "<head><title>CGI Configuration Error</title></head>\n"
    "<body><h1>CGI Configuration Error</h1>\n"
    "The CGI program %s is writable by users other than its owner.\n"
    "</body>\n", zRealScript);
  MakeLogEntry(0);
  exit(0);       
}

/*
** Tell the client that the server malfunctioned.
*/
static void Malfunction(int linenum){
  StartResponse("500 Server Malfunction");
  nOut += printf(
    "Content-type: text/html\r\n"
    "\r\n"
    "<head><title>Server Malfunction</title></head>\n"
    "<body><h1>Server Malfunction</h1>\n"
    "This web server has malfunctioned.\n"
    "(Error number: %d)\n"
    "</body>\n", linenum);
  MakeLogEntry(0);
  exit(0);       
}

/*
** Do a server redirect to the document specified.  The document
** name not contain scheme or network location or the query string.
** It will be just the path.
*/
static void Redirect(const char *zPath, int finish){
  StartResponse("302 Temporary Redirect");
  if( zServerPort==0 || zServerPort[0]==0 || strcmp(zServerPort,"80")==0 ){
    nOut += printf("Location: http://%s%s%s\r\n",
                   zServerName, zPath, zQuerySuffix);
  }else{
    nOut += printf("Location: http://%s:%s%s%s\r\n",
                   zServerName, zServerPort, zPath, zQuerySuffix);
  }
  if( finish ){
    nOut += printf("\r\n");
    MakeLogEntry(0);
  }
}

/*
** This array maps file suffixes into MIME-types
*/
static struct Suffix {
  int len;
  char *zSuffix;
  char *zContentType;
} suffix[] = {
  {  5,     ".html",    "text/html" },
  {  4,     ".htm",     "text/html" },
  {  4,     ".css",     "text/css"  },
  {  4,     ".gif",     "image/gif" },
  {  5,     ".jpeg",    "image/jpeg" },
  {  4,     ".jpg",     "image/jpeg" },
  {  4,     ".png",     "image/png"  },
};

/*
** Deduce the mime-type of the document to be delivered from its
** name suffix.  First check the suffix against the mime-types
** in the suffix[] array defined above.  If no match is found,
** then check the file named "mimetimes" in the home directory
** of the server (named by the zHome global variable.)  If that
** file doesn't exist, or there still is no match, then set the
** mime-type to text/plain.
*/
static char *GetMimeType(
  const char *z    /* The name of a file for which the mime type is sought */
){
  int i;                 /* Loop counter */
  char *zTypeDbName;     /* Name of the mime-type database file */
  FILE *in;              /* For reading the mime-type database file */
  int suflen;            /* Length of a suffix */
  char zSuffix[100];     /* Text of a suffix */
  char zType[800];       /* Name of a mime-type in the database file */
  char zLine[2000];      /* An input line of the mem-type database file */

  /* First check the built-in suffix table
  */
  for(i=0; i<sizeof(suffix)/sizeof(suffix[0]); i++){
    suflen = suffix[i].len;
    if( lenFile>suflen && strcmp(&zFile[lenFile-suflen],suffix[i].zSuffix)==0 ){
      return suffix[i].zContentType;
    }
  }

  /* Next try the "mimetypes" file in the home directory.
  */
  zTypeDbName = malloc( strlen(zHome) + 20 );
  if( zTypeDbName ){
    sprintf(zTypeDbName,"%s/mimetypes",zHome);
    in = fopen(zTypeDbName,"r");
    if( in ){
      while( fgets(zLine,sizeof(zLine),in) ){
        if( zLine[0]=='#' ) continue;
        if( isspace(zLine[0]) ) continue;
        if( sscanf(zLine,"%98[^ ] %798[^ \n]",zSuffix,zType)!=2 ) continue;
        suflen = strlen(zSuffix);
        if( lenFile > suflen && strcmp(&zFile[lenFile-suflen],zSuffix)==0 ){
          return StrDup(zType);
        }
      }
      fclose(in);
    }
    free(zTypeDbName);
  }

  /* When all else fails, assume a mime type of "text/plain"
  */
  return "text/plain";
}

/*
** The following table contains 1 for all characters that are permitted in
** the part of the URL before the query parameters and fragment.
*/
static const char allowedInName[] = {
      /*  x0  x1  x2  x3  x4  x5  x6  x7  x8  x9  xa  xb  xc  xd  xe  xf */
/* 0x */   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
/* 1x */   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
/* 2x */   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
/* 3x */   1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  0,  0,  0,
/* 4x */   0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
/* 5x */   1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  0,  1,
/* 6x */   0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
/* 7x */   1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  1,  0,
};

/*
** This routine processes a single HTTP request on standard input and
** sends the reply to standard output.  If the argument is 1 it means
** that we are should close the socket without processing additional
** HTTP requests after the current request finishes.  0 means we are
** allowed to keep the connection open and to process additional requests.
** This routine may choose to close the connection even if the argument
** is 0.
** 
** If the connection should be closed, this routine calls exit() and
** thus never returns.  If this routine does return it means that another
** HTTP request may appear on the wire.
*/
void ProcessOneRequest(int forceClose){
  int i, c;
  char *z;                  /* Used to parse up a string */
  struct stat statbuf;      /* Information about the file to be retrieved */
  FILE *in;                 /* For reading from CGI scripts */
  char zLine[1000];         /* A buffer for input lines or forming names */

  /* Change directories to the root of the HTTP filesystem
  */
  if( chdir(zRoot[0] ? zRoot : "/")!=0 ){
    Malfunction(__LINE__);
  }
  nRequest++;

  /*
  ** We must receive a complete header within 15 seconds
  */
  signal(SIGALRM, Timeout);
  alarm(15);

  /* Get the first line of the request and parse out the
  ** method, the script and the protocol.
  */
  if( fgets(zLine,sizeof(zLine),stdin)==0 ){
    exit(0);
  }
  omitLog = 0;
  nIn += strlen(zLine);
  zMethod = StrDup(GetFirstElement(zLine,&z));
  zRealScript = zScript = StrDup(GetFirstElement(z,&z));
  zProtocol = StrDup(GetFirstElement(z,&z));
  if( zProtocol==0 || strncmp(zProtocol,"HTTP/",5)!=0 || strlen(zProtocol)!=8 ){
    StartResponse("400 Bad Request");
    nOut += printf(
      "Content-type: text/html\r\n"
      "\r\n"
      "<title>Unknown Protocol On HTTP Request</title>\n"
      "<h1>Unknown Protocol</h1>\n"
      "This server does not understand the requested protocol\n"
    );
    MakeLogEntry(0);
    exit(0);
  }
  if( forceClose ){
    closeConnection = 1;
  }else if( zProtocol[5]<'1' || zProtocol[7]<'1' ){
    closeConnection = 1;
  }

  /* This very simple server only understands the GET, POST
  ** and HEAD methods
  */
  if( strcmp(zMethod,"GET")!=0 && strcmp(zMethod,"POST")!=0
       && strcmp(zMethod,"HEAD")!=0 ){
    StartResponse("501 Not Implemented");
    nOut += printf(
      "Content-type: text/html\r\n"
      "\r\n"
      "<head><title>Method not implemented</title></head>\n"
      "<body><h1>Method not implemented</h1>\n"
      "The %s method is not implemented on this server.\n" 
      "</body>\n",
      zMethod);
    MakeLogEntry(0);
    exit(0);
  }

  /* Get all the optional fields that follow the first line.
  */
  zCookie = 0;
  while( fgets(zLine,sizeof(zLine),stdin) ){
    char *zFieldName;
    char *zVal;

    nIn += strlen(zLine);
    zFieldName = GetFirstElement(zLine,&zVal);
    if( zFieldName==0 || *zFieldName==0 ) break;
    RemoveNewline(zVal);
    if( strcasecmp(zFieldName,"User-Agent:")==0 ){
      zAgent = StrDup(zVal);
      FixupUserAgent();
    }else if( strcasecmp(zFieldName,"Accept:")==0 ){
      zAccept = StrDup(zVal);
    }else if( strcasecmp(zFieldName,"Content-length:")==0 ){
      zContentLength = StrDup(zVal);
    }else if( strcasecmp(zFieldName,"Content-type:")==0 ){
      zContentType = StrDup(zVal);
    }else if( strcasecmp(zFieldName,"Referer:")==0 ){
      zReferer = StrDup(zVal);
    }else if( strcasecmp(zFieldName,"Cookie:")==0 ){
      zCookie = StrAppend(zCookie,"; ",zVal);
    }else if( strcasecmp(zFieldName,"Connection:")==0 ){
      if( strcasecmp(zVal,"close")==0 ){
        closeConnection = 1;
      }else if( !forceClose && strcasecmp(zVal, "keep-alive")==0 ){
        closeConnection = 0;
      }
    }else if( strcasecmp(zFieldName,"Host:")==0 ){
      zServerPort = zServerName = zHttpHost = StrDup(zVal);
      while( zServerPort && *zServerPort && *zServerPort!=':' ){
        zServerPort++;
      }
      if( zServerPort && *zServerPort ){
        *zServerPort = 0;
        zServerPort++;
      }
      if( zRealPort ){
        zServerPort = StrDup(zRealPort);
      }
    }
  }

  /* Make an extra effort to get a valid server name and port number.
  ** Only Netscape provides this information.  If the browser is
  ** Internet Explorer, then we have to find out the information for
  ** ourselves.
  */
  if( zServerName==0 ){
    zServerName = SafeMalloc( 100 );
    gethostname(zServerName,100);
  }
  if( zServerPort==0 || *zServerPort==0 ){
    zServerPort = DEFAULT_PORT;
  }

  /* Remove the query string from the end of the requested file.
  */
  for(z=zScript; *z && *z!='?'; z++){}
  if( *z=='?' ){
    zQuerySuffix = StrDup(z);
    *z = 0;
  }else{
    zQuerySuffix = "";
  }
  zQueryString = *zQuerySuffix ? &zQuerySuffix[1] : zQuerySuffix;

  /* Create a file to hold the POST query data, if any.  We have to
  ** do it this way.  We can't just pass the file descriptor down to
  ** the child process because the fgets() function may have already
  ** read part of the POST data into its internal buffer.
  */
  if( zMethod[0]=='P' && zContentLength!=0 ){
    int len = atoi(zContentLength);
    FILE *out;
    char *zBuf;
    int n;

    if( len>MAX_CONTENT_LENGTH ){
      StartResponse("500 Request too large");
      nOut += printf(
        "Content-type: text/html\r\n"
        "\r\n"
        "Too much POST data\n"
        "</body>\n"
      );
      MakeLogEntry(0);
      exit(0);
    }
    sprintf(zTmpNamBuf, "/tmp/-post-data-XXXXXX");
    zTmpNam = zTmpNamBuf;
    mkstemp(zTmpNam);
    out = fopen(zTmpNam,"w");
    zBuf = SafeMalloc( len );
    alarm(15 + len/2000);
    n = fread(zBuf,1,len,stdin);
    nIn += n;
    fwrite(zBuf,1,n,out);
    free(zBuf);
    fclose(out);
  }

  /* Make sure the running time is not too great */
  alarm(10);

  /* Convert all unusual characters in the script name into "_".
  **
  ** This is a defense against various attacks, XSS attacks in particular.
  */
  for(z=zScript; *z; z++){
    unsigned char c = *(unsigned char*)z;
    if( (c&0x80)!=0 || !allowedInName[c] ){
      *z = '_';
    }
  }

  /* Don't allow "/." or "/-" to to occur anywhere in the entity name.
  ** This prevents attacks involving ".." and also allows us to create
  ** files and directories whose names begin with "-" which are invisible
  ** to the webserver.
  */
  for(z=zScript; *z; z++){
    if( *z=='/' && (z[1]=='.' || z[1]=='-') ){
       NotFound(__LINE__);
    }
  }

  /* Figure out what the root of the filesystem should be.  If the
  ** HTTP_HOST parameter exists (stored in zHttpHost) then remove the
  ** port number from the end (if any), convert all characters to lower
  ** case, and convert all "." to "_".  Then try to find a directory
  ** with that name and the extension .website.  If not found, look
  ** for "default.website".
  */
  if( zScript[0]!='/' ) NotFound(__LINE__);
  if( strlen(zRoot)+40 >= sizeof(zLine) ) NotFound(__LINE__);
  if( zHttpHost==0 ){
    sprintf(zLine, "%s/default.website", zRoot);
  }else if( strlen(zHttpHost)+strlen(zRoot)+10 >= sizeof(zLine) ){
    NotFound(__LINE__);
  }else{
    sprintf(zLine, "%s/%s", zRoot, zHttpHost);
    for(i=strlen(zRoot)+1; zLine[i] && zLine[i]!=':'; i++){
      int c = zLine[i];
      if( !isalnum(c) ){
        zLine[i] = '_';
      }else if( isupper(c) ){
        zLine[i] = tolower(c);
      }
    }
    strcpy(&zLine[i], ".website");
  }
  if( stat(zLine,&statbuf) && !S_ISDIR(statbuf.st_mode) ){
    sprintf(zLine, "%s/default.website", zRoot);
    if( stat(zLine,&statbuf) && !S_ISDIR(statbuf.st_mode) ){
      NotFound(__LINE__);
    }
  }
  
  zHome = StrDup(zLine);

  /* Change directories to the root of the HTTP filesystem
  */
  if( chdir(zHome)!=0 ){
    Malfunction(__LINE__);
  }

  /* Locate the file in the filesystem.  We might have to append
  ** the name "index.html" in order to find it.  Any excess path
  ** information is put into the zPathInfo variable.
  */
  zLine[0] = '.';
  i = 0;
  while( zScript[i] ){
    while( zScript[i] && zScript[i]!='/' ){
      zLine[i+1] = zScript[i];
      i++;
    }
    zLine[i+1] = 0;
    if( stat(zLine,&statbuf)!=0 ){
      NotFound(__LINE__);
    }
    if( S_ISREG(statbuf.st_mode) ){
      if( access(zLine,R_OK) ){
        NotFound(__LINE__);
      }
      zRealScript = StrDup(&zLine[1]);
      break;
    }
    if( zScript[i]==0 || zScript[i+1]==0 ){
      strcpy(&zLine[i+1],"/index.html");
      if( stat(zLine,&statbuf)!=0 || !S_ISREG(statbuf.st_mode) 
      || access(zLine,R_OK) ){
        strcpy(&zLine[i+1],"/index.cgi");
        if( stat(zLine,&statbuf)!=0 || !S_ISREG(statbuf.st_mode) 
        || access(zLine,R_OK) ){
          NotFound(__LINE__);
        }
      }
      zRealScript = StrDup(&zLine[1]);
      if( zScript[i]==0 ){
        /* If the requested URL does not end with "/" but we had to
        ** append "index.html", then a redirect is necessary.  Otherwise
        ** none of the relative URLs in the delivered document will be
        ** correct. */
        Redirect(zRealScript, 1);
        return;
      }
      break;
    }
    zLine[i+1] = zScript[i];
    i++;
  }
  zFile = StrDup(zLine);
  zPathInfo = StrDup(&zScript[i]);
  lenFile = strlen(zFile);
  zDir = StrDup(zFile);
  for(i=strlen(zDir)-1; i>0 && zDir[i]!='/'; i--){};
  if( i==0 ){
     strcpy(zDir,"/");
  }else{
     zDir[i] = 0;
  }

  /* Take appropriate action
  */
  if( (statbuf.st_mode & 0100)==0100 && access(zFile,X_OK)==0 ){
    /*
    ** The followings static variables are used to setup the environment
    ** for the CGI script
    */
    static char *default_path = "/bin:/usr/bin";
    static char *gateway_interface = "CGI/1.0";
    static struct {
      char *zEnvName;
      char **pzEnvValue;
    } cgienv[] = {
      { "CONTENT_LENGTH",              &zContentLength },
      { "CONTENT_TYPE",                &zContentType },
      { "DOCUMENT_ROOT",               &zHome },
      { "GATEWAY_INTERFACE",           &gateway_interface },
      { "HTTP_ACCEPT",                 &zAccept },
      { "HTTP_COOKIE",                 &zCookie },
      { "HTTP_HOST",                   &zHttpHost },
      { "HTTP_REFERER",                &zReferer },
      { "HTTP_USER_AGENT",             &zAgent },
      { "PATH",                        &default_path },
      { "PATH_INFO",                   &zPathInfo },
      { "QUERY_STRING",                &zQueryString },
      { "REMOTE_ADDR",                 &zRemoteAddr },
      { "REQUEST_METHOD",              &zMethod },
      { "REQUEST_URI",                 &zScript },
      { "SCRIPT_DIRECTORY",            &zDir },
      { "SCRIPT_FILENAME",             &zFile },
      { "SCRIPT_NAME",                 &zRealScript },
      { "SERVER_NAME",                 &zServerName },
      { "SERVER_PORT",                 &zServerPort },
      { "SERVER_PROTOCOL",             &zProtocol },
    };
    char *zBaseFilename;   /* Filename without directory prefix */

    /* If its executable, it must be a CGI program.  Start by
    ** changing directories to the directory holding the program.
    */
    if( chdir(zDir) ){
      Malfunction(__LINE__);
    }

    /* Setup the environment appropriately.
    */
    for(i=0; i<sizeof(cgienv)/sizeof(cgienv[0]); i++){
      if( *cgienv[i].pzEnvValue ){
        SetEnv(cgienv[i].zEnvName,*cgienv[i].pzEnvValue);
      }
    }

    /*
    ** Abort with an error if the CGI script is writable by anyone other
    ** than its owner.
    */
    if( statbuf.st_mode & 0022 ){
      CgiScriptWritable();
    }

    /* For the POST method all input has been written to a temporary file,
    ** so we have to redirect input to the CGI script from that file.
    */
    if( zMethod[0]=='P' ){
      dup(0);
      close(0);
      open(zTmpNam, O_RDONLY);
    }

    for(i=strlen(zFile)-1; i>=0 && zFile[i]!='/'; i--){}
    zBaseFilename = &zFile[i+1];
    if( i>=0 && strncmp(zBaseFilename,"nph-",4)==0 ){
      /* If the name of the CGI script begins with "nph-" then we are
      ** dealing with a "non-parsed headers" CGI script.  Just exec()
      ** it directly and let it handle all its own header generation.
      */
      execl(zBaseFilename,zBaseFilename,(char*)0);
      /* NOTE: No log entry written for nph- scripts */
      exit(0);
    }

    /* Fall thru to here only if this process (the server) is going
    ** to read and augment the header sent back by the CGI process.
    ** Open a pipe to receive the output from the CGI process.  Then
    ** fork the CGI process.  Once everything is done, we should be
    ** able to read the output of CGI on the "in" stream.
    */
    {
      int px[2];
      pipe(px);
      if( fork()==0 ){
        close(px[0]);
        close(1);
        dup(px[1]);
        close(px[1]);
        execl(zBaseFilename, zBaseFilename, (char*)0);
        exit(0);
      }
      close(px[1]);
      in = fdopen(px[0], "r");
    }
    if( in==0 ){
      CgiError();
    }

    /* Read and process the first line of the header returned by the
    ** CGI script.
    */
    alarm(15);
    while( fgets(zLine,sizeof(zLine),in) ){
      if( strncmp(zLine,"Location:",9)==0 ){
        int i;
        RemoveNewline(zLine);
        z = &zLine[10];
        while( isspace(*z) ){ z++; }
        for(i=0; z[i]; i++){
          if( z[i]=='?' ){
            zQuerySuffix = StrDup("");
          }
        }
        
        if( z[0]=='/' && z[1]=='/' ){
          /* The scheme is missing.  Add it in before redirecting */
          StartResponse("302 Redirect");
          nOut += printf("Location: http:%s%s\r\n",z,zQuerySuffix);
          break; /* DK */
          MakeLogEntry(0);
          return;
        }else if( z[0]=='/' ){
          /* The scheme and network location are missing but we have
          ** an absolute path. */
          Redirect(z, 0); /* DK */
          break;
        }
        /* Check to see if there is a scheme prefix */
        for(i=0; z[i] && z[i]!=':' && z[i]!='/'; i++){}
        if( z[i]==':' ){
          /* We have a scheme.  Assume there is an absolute URL */
          StartResponse("302 Redirect");
          nOut += printf("Location: %s%s\r\n",z,zQuerySuffix);
          break; /* DK */
          MakeLogEntry(0);
          return;
        }
        /* Must be a relative pathname.  Construct the absolute pathname
        ** and redirect to it. */
        i = strlen(zRealScript);
        while( i>0 && zRealScript[i-1]!='/' ){ i--; }
        while( i>0 && zRealScript[i-1]=='/' ){ i--; }
        while( *z=='.' ){
          if( z[1]=='/' ){
            z += 2;
          }else if( z[1]=='.' && z[2]=='/' ){
            while( i>0 && zRealScript[i-1]!='/' ){ i--; }
            while( i>0 && zRealScript[i-1]=='/' ){ i--; }
            z += 3;
          }else{
            break;
          }
        }
        StartResponse("302 Redirect");
        nOut += printf("Location: http://%s",zServerName);
        if( strcmp(zServerPort,"80") ){
          nOut += printf(":%s",zServerPort);
        }
        nOut += printf("%.*s/%s%s\r\n\r\n",i,zRealScript,z,zQuerySuffix);
        MakeLogEntry(0);
        return;
      }else if( strncmp(zLine,"Status:",7)==0 ){
        int i;
        for(i=7; isspace(zLine[i]); i++){}
        nOut += printf("%s %s", zProtocol, &zLine[i]);
        strncpy(zReplyStatus, &zLine[i], 3);
        zReplyStatus[3] = 0;
        statusSent = 1;
        break;
      }else{
        int i;
        StartResponse("200 OK");
        nOut += printf("%s",zLine);
        for(i=0; zLine[i] && !isspace(zLine[i]) && zLine[i]!=':'; i++){}
        if( i<2 || zLine[i]!=':' ) break;
      }
    }

    /* Copy everything else thru without change or analysis.
    */
    alarm(60*5);
    while( (c = getc(in))!=EOF ){
      putc(c,stdout);
      nOut++;
    }
    fclose(in);
  }else{
    /* If it isn't executable then it
    ** must a simple file that needs to be copied to output.
    */
    char *zContentType = GetMimeType(zFile);
    off_t offset;

    if( zTmpNam ) unlink(zTmpNam);
    in = fopen(zFile,"r");
    if( in==0 ) NotFound(__LINE__);
    StartResponse("200 OK");
    nOut += DateTag("Last-Modified", statbuf.st_mtime);
    nOut += printf("Content-type: %s\r\n",zContentType);
    nOut += printf("Content-length: %d\r\n\r\n",(int)statbuf.st_size);
    fflush(stdout);
    if( strcmp(zMethod,"HEAD")==0 ){
      MakeLogEntry(0);
      fclose(in);
      return;
    }
    alarm(30 + statbuf.st_size/1000);
#ifdef linux
    offset = 0;
    nOut += sendfile(fileno(stdout), fileno(in), &offset, statbuf.st_size);
#else
    while( (c = getc(in))!=EOF ){
      putc(c,stdout);
      nOut++;
    }
#endif
    fclose(in);
  }
  fflush(stdout);
  MakeLogEntry(0);

  /* The next request must arrive within 30 seconds or we close the connection
  */
  omitLog = 1;
  alarm(30);
}


int main(int argc, char **argv){
  int i;                    /* Loop counter */
  char *zPermUser = 0;      /* Run daemon with this user's permissions */

  /* Record the time when processing begins.
  */
  time(&beginTime);

  /* Parse command-line arguments
  */
  while( argc>2 && argv[1][0]=='-' ){
    if( strcmp(argv[1],"-user")==0 ){
      zPermUser = argv[2];
      argv += 2;
      argc -= 2;
    }else if( strcmp(argv[1],"-root")==0 ){
      zRoot = argv[2];
      argv += 2;
      argc -= 2;
    }else if( strcmp(argv[1],"-logfile")==0 ){
      zLogFile = argv[2];
      argv += 2;
      argc -= 2;
    }else{
      Malfunction(__LINE__);
    }
  }
  if( zRoot==0 ){
    Malfunction(__LINE__);
  }
  
  /* Change directories to the root of the HTTP filesystem
  */
  if( chdir(zRoot)!=0 ){
    Malfunction(__LINE__);
  }

  /* Attempt to go into a chroot jail as user zPermUser
  */
  if( zPermUser ){
    struct passwd *pwd = getpwnam(zPermUser);
    if( pwd ){
      if( chroot(".")<0 ) Malfunction(__LINE__);
      setgid(pwd->pw_gid);
      setuid(pwd->pw_uid);
      zRoot = "";
    }else{
      Malfunction(__LINE__);
    }
  }
  if( getuid()==0 ){
    Malfunction(__LINE__);
  }

  /* Get the IP address from when the request originates
  */
  {
    struct sockaddr_in remoteName;
    int size = sizeof(struct sockaddr_in);
    if( getpeername(fileno(stdin), (struct sockaddr*)&remoteName, &size)>=0 ){
      zRemoteAddr = StrDup(inet_ntoa(remoteName.sin_addr));
    }
  }

  /* Process the input stream */
  for(i=0; i<100; i++){
    ProcessOneRequest(0);
  }
  ProcessOneRequest(1);
  exit(0);
}
