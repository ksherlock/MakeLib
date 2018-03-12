/***************************************************************
*
*  ORCA/M MakeLIB 2.0
*
*  By Mike Westerfield
*  Original Version 4 April 1988
*  2.0 Version: August 1991
*
*  Copyright 1986,1988,1991
*  By the Byte Works, Inc.
*
*  The source code for APW is protected by trade secret.  It
*  cannot be released or used in any way except for building
*  APW and archiving the source without the written permission
*  of the Byte Works, Inc.
*
*  Written using ORCA/C 1.2.
*
****************************************************************
*
*  MAKELIB [-D] [-F] library [+|-|^ object]
*
*	-D	  indicates that the dictionary should be listed
*	-F	  indicates that the list of files should be listed
*	library	  name of the library to modify
*	+object	  add the object file to the library
*	-object	  remove the object file from the library
*	^object	  remove the object file and write it as an object file
*
***************************************************************/

#pragma keep "makelib"
#pragma lint -1
#pragma optimize 9

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#define TRUE 1				/* boolean constants */
#define FALSE 0
#define BOOL int 			/* boolean type */

#define MAXLINE 255			/* size of input line */
#define MAXNAME 256			/* # chars in a symbol or file name */
#define MAXFILE 8192			/* max length of a path name */

					/* Shell, GS/OS Interface */
                                        /**************************/
extern pascal void PDosInt(int, char *);
#define EXPAND_DEVICES(parm)	(PDosInt(0x0154,parm))
#define WRITE_CONSOLE(parm)     (PDosInt(0x015A,parm))
#define DestroyGS(pBlockPtr)      (PDosInt(0x2002,pBlockPtr))
#define SetFileInfoGS(pBlockPtr)  (PDosInt(0x2005,pBlockPtr))
#define GetFileInfoGS(pBlockPtr)  (PDosInt(0x2006,pBlockPtr))

typedef unsigned char byte, Byte;

struct TimeRec {
   Byte second;
   Byte minute;
   Byte hour;
   Byte year;
   Byte day;
   Byte month;
   Byte extra;
   Byte weekDay;
   } ;
typedef struct TimeRec TimeRec, *TimeRecPtr, **TimeRecHndl;

struct GSString255 {
   int length;
   char text[255];
   } ;
typedef struct GSString255 GSString255, *GSString255Ptr, **GSString255Hndl;

struct ResultBuf255 {
   int bufSize;
   GSString255 bufString;
   } ;
typedef struct ResultBuf255 ResultBuf255, *ResultBuf255Ptr, **ResultBuf255Hndl;

struct FileInfoRecGS {
   int pCount;
   GSString255Ptr pathname;
   int access;
   int fileType;
   long auxType;
   int storageType; /* must be 0 for SetFileInfo */
   TimeRec createDateTime;
   TimeRec modDateTime;
   ResultBuf255Ptr optionList;
   long eof;
   long blocksUsed; /* must be 0 for SetFileInfo */
   long resourceEOF; /* must be 0 for SetFileInfo */
   long resourceBlocks; /* must be 0 for SetFileInfo */
   } ;
typedef struct FileInfoRecGS FileInfoRecGS, *FileInfoRecPtrGS;

typedef struct osName {			/* GS/OS input name */
   int length;
   char str[MAXFILE];
   } osName, *osNamePtr;

typedef struct outName {		/* GS/OS output name */
   int buffsize;
   int length;
   char str[MAXFILE];
   } outName, *outNamePtr;

struct NameRecGS {
   int pCount;
   GSString255Ptr pathname;
   } ;
typedef struct NameRecGS NameRecGS, *NameRecPtrGS;

					/* file handling */
					/*****************/
FILE	*inFile;			/* input file */
FILE	*outFile; 			/* output file */
FILE	*objFile; 			/* obj output file for ^ */

byte	*s,*s2;				/* disk segment */

long	ds,nds;				/* disp in s */

char	(*libName)[MAXFILE];		/* file names */
char	(*segName)[MAXFILE];
char	segFlag; 			/* object segment processing type */

					/* symbol tables */
					/*****************/

typedef struct fileStruct {		/* file list */
   struct fileStruct *fNext;
   char *fName;
   int fFile;
   } fileStruct;

typedef struct symbolStruct {		/* symbol table entry */
   struct symbolStruct *sNext;		/* next entry */
   char *sName;				/* symbol name */
   int sFile;				/* file index number */
   long sSeg;
   BOOL sPrivate;			/* private symbol? */
   } symbolStruct;

symbolStruct *symbol;			/* pointer to first symbol */
fileStruct *file;			/* pointer to file entry */

					/* misc variables and flags */
					/****************************/
int	addFNum; 			/* file # of added file */
int	argcnt;				/* index of last argv */
int	largc;				/* copy of argc */
char	**largv; 			/* copy of argv */
char	line[MAXLINE];			/* input line buffer */
BOOL	printDictionary; 		/* print the dictionary? */
BOOL	printFiles;			/* print the list of files? */
BOOL	progress;			/* print progress info? */
int	status;				/* status sent back to shell */

/* Spinner */
/*---------*/

#define spinSpeed 3			/* calls before one spinner move */

int spinning = 0;			/* are we spinning now? */
int spinDisp = 0;			/* disp to the spinner character */
int spinCount = 0;			/* spin loop counter */

int spinner[] = {0x7C,0x2F,0x2D,0x5C};	/* spinner characters */

/* Global utility subroutines *********************************/

/***************************************************************
*
*  Spin - spin the spinner
*
*  Notes: Starts the spinner if it is not already in use.
*
***************************************************************/

void Spin (void)

{
struct {
   int pcount;
   char ch;
   } parm;

if (!spinning) {
   spinning = TRUE;
   spinCount = spinSpeed;
   }

if (! (--spinCount)) {
   spinCount = spinSpeed;
   --spinDisp;
   if (spinDisp < 0)
      spinDisp = 3;
   parm.pcount = 1;
   parm.ch = (char) spinner[spinDisp];
   WRITE_CONSOLE((void *) &parm);
   parm.ch = (char) 8;
   WRITE_CONSOLE((void *) &parm);
   }
}

/***************************************************************
*
*  StopSpin - stop the spinner
*
*  Notes: The call is safe, and ignored, if the spinner is inactive
*
***************************************************************/

void StopSpin (void)

{
struct {
   int pcount;
   char ch;
   } parm;

if (spinning) {
   spinning = FALSE;
   parm.pcount = 1;
   parm.ch = ' ';
   WRITE_CONSOLE((void *) &parm);
   parm.ch = (char) 8;
   WRITE_CONSOLE((void *) &parm);
   }
}


/***************************************************************
*
*  CheckESC - Checks to see if 'escape' is pressed
*
*  Notes: Returns to shell if open-apple period has been pressed.
*
***************************************************************/

void CheckESC (void)

{
char *keyboard;
char *strobe;
char *flags;
struct {
   int pcount;
   char ch;
   } parm;

keyboard = (void *) 0x00C000;
strobe = (void *) 0x00C010;
flags = (void *) 0x00C025;

if (*keyboard & 0x80) {
   if ((*keyboard & 0x7F) == '.')
      if (*flags & 0x80) {
         *strobe = (char) 0;
         exit(-1);
         }
   *strobe = (char) 0;
   parm.pcount = 1;
   parm.ch = (char)   27; WRITE_CONSOLE((void *) &parm);
   parm.ch = (char)   15; WRITE_CONSOLE((void *) &parm);
   parm.ch = (char) 0x43; WRITE_CONSOLE((void *) &parm);
   parm.ch = (char)   24; WRITE_CONSOLE((void *) &parm);
   parm.ch = (char)   14; WRITE_CONSOLE((void *) &parm);
   parm.ch = (char)    8; WRITE_CONSOLE((void *) &parm);
   while (! (*keyboard & 0x80)) ;
   parm.ch = (char)   32; WRITE_CONSOLE((void *) &parm);
   parm.ch = (char)    8; WRITE_CONSOLE((void *) &parm);
   if ((*keyboard & 0x7F) == '.')
      if (*flags & 0x80) {
         *strobe = (char) 0;
         exit(-1);
         }        
   *strobe = (char) 0;
   }
}

/***************************************************************
*
*  Error - writes an error message
*
*  Inputs:
*	num - error number
*	stat - value to set status flag
*	str - string to acompany error (some errors only)
*
***************************************************************/

void Error (int num, int stat, char *str)

{
status = stat;				/* set status value */

switch (num) {
   case 1:
      fprintf(stderr, "\nThe object file %s is already in the library.\n", str);
      break;
   case 2:
      fprintf(stderr, "Out of memory.\n");
      break;
   case 3:
      fprintf(stderr, "%s is not an object module.\n", str);
      break;
   case 4:
      fprintf(stderr, "Could not open %s.\n", str);
      break;
   case 5:
      fprintf(stderr, "Unsupported version of the OMF.\n");
      break;
   case 6:
      fprintf(stderr, "Object module format error.\n");
      break;
   case 7:
      fprintf(stderr, "Error writing to work file.\n");
      break;
   case 8:
      fprintf(stderr, "\nThe object file %s is not in the library.\n", str);
      break;
   case 9:
      fprintf(stderr, "Missing operation on %s.\n", str);
      break;
   case 10:
      fprintf(stderr, "No action requested.\n");
      break;
   case 11:
      fprintf(stderr, "Could not open the work file %s.\n", str);
      break;
   case 12:
      fprintf(stderr, "Illegal operation on %s.\n", str);
      break;
   case 13:
      fprintf(stderr, "%s is already in the symbol table.\n", str);
      break;
   case 14:
      fprintf(stderr, "\nThe object file %s could not be opened for output.\n",
	 str);
      break;
   case 15:
      fprintf(stderr, "Could not open the library %s.\n", str);
      break;
   case 16:
      fprintf(stderr, "%s is not a library file.\n", str);
    }
}

/***************************************************************
*
*  tmalloc - allocate memory and check for out of memory
*
*  Parameters:
*	size - number of bytes to allocate
*
*  Returns:
*	pointer to the memory
*
***************************************************************/

void * tmalloc (size_t size)

{
void *ptr;

ptr = malloc(size);
if (ptr == NULL) {
   Error(2,-1,0);
   exit(-1);
   }
return ptr;
}

/* ReadDictionary and Init and support subroutines ************/

/***************************************************************
*
*  CopyName - copy a name from *s to *s2
*
*  Inputs:
*	len - length of named entries
*
***************************************************************/

void CopyName (long len)

{
int i;

if (!len) {
   len = s[ds++];
   s2[nds++] = len;
   }
for (i = 0; i < len; ++i)
   s2[nds++] = s[ds++];
}

/***************************************************************
*
*  CopyNumber - copy a number from *s to *s2
*
***************************************************************/

long CopyNumber (void)

{
int i,sc,numlen;
long num;

numlen = s[14];
num = 0;
if (s[32])
   for (i = 0; i < numlen; ++i) {
      s2[nds++] = s[ds];
      num = (num<<8)|s[ds++];
      }
else {
   sc = 0;
   for (i = 0; i < numlen; ++i) {
      s2[nds++] = s[ds];
      num = num|(s[ds++]<<sc);
      sc += 8;
      }
   }
return num;
}

/***************************************************************
*
*  CopyExpression - copy an expression from *s to *s2
*
*  Inputs:
*	lablen - length of labels
*	numlen - length of numbers
*
***************************************************************/

void CopyExpression (int lablen, int numlen)

{
int opcode,i;

do {
   opcode = s[ds++];
   s2[nds++] = opcode;
   if (opcode & 128)
      switch (opcode) {
	 case 129:			/* $81 constant */
	 case 135:			/* $87 relative offset */
            for (i = 0; i < numlen; ++i)
               s2[nds++] = s[ds++];
            break;
	 case 130:			/* $82 weak reference */
	 case 131:			/* $83 label */
	 case 132:			/* $84 length attribute */
	 case 133:			/* $85 type attribute */
	 case 134:			/* $86 count attribute */
            CopyName(lablen);
	 }
   }
while (opcode);
}

/***************************************************************
*
*  Read2 - read a 2 byte number from a segment
*
*  Inputs:
*	s - segment
*	ds - position to read from
*
*  Outputs:
*	ds - updated
*
*  Returns:
*	number read
*
***************************************************************/

int Read2 (void)

{
int num;

num = 0;
if (s[32])
   num = (s[ds++]<<8)+s[ds++];
else
   num = s[ds++]+(s[ds++]<<8);
return num;
}

/***************************************************************
*
*  Read4 - read a 4 byte number from a segment
*
*  Inputs:
*	s - segment
*	ds - position to read from
*
*  Outputs:
*	ds - updated
*
*  Returns:
*	number read
*
***************************************************************/

long Read4 (void)

{
long num;

num = 0;
if (s[32])
   num = (((unsigned long) s[ds++])<<24) | (((unsigned long) s[ds++])<<16)
      | (s[ds++]<<8) | s[ds++];
else
   num = s[ds++] | (s[ds++]<<8) | (((unsigned long) s[ds++])<<16)
      | (((unsigned long) s[ds++])<<24);
return num;
}

/***************************************************************
*
*  ReadName - read a name from the input file
*
*  Inputs:
*	len - length of named entries
*	name - pointer to location to store the name
*
***************************************************************/

void ReadName (char *name, long len)

{
int i;

if (!len)
   len = s[ds++];
for (i = 0; i < len; ++i)
   name[i] = s[ds++];
while (name[len-1] == ' ') --len;
name[len] = (char) 0;
}

/***************************************************************
*
*  ReadNumber - read a number from a segment
*
*  Inputs:
*	s - segment
*	ds - position to read from
*
*  Outputs:
*	ds - updated
*
*  Returns:
*	number read
*
***************************************************************/

long ReadNumber (void)

{
int i,sc,numlen;
long num;

numlen = s[14];
num = 0;
if (s[32])
   for (i = 0; i < numlen; ++i)
      num = (num<<8)|s[ds++];
else {
   sc = 0;
   for (i = 0; i < numlen; ++i) {
      num = num|(s[ds++]<<sc);
      sc += 8;
      }
   }
return num;
}

/***************************************************************
*
*  ReadExpression - read an expression from the object file
*
*  Inputs:
*	lablen - length of labels
*	numlen - length of numbers
*	inFile - input file
*
***************************************************************/

void ReadExpression (int lablen, int numlen)

{
int opcode,i;
char name[MAXNAME];

do {
   opcode = s[ds++];
   if (opcode & 128)
      switch (opcode) {
	 case 129:			/* $81 constant */
	 case 135:			/* $87 relative offset */
	    ds += numlen;
            break;
	 case 130:			/* $82 weak reference */
	 case 131:			/* $83 label */
	 case 132:			/* $84 length attribute */
	 case 133:			/* $85 type attribute */
	 case 134:			/* $86 count attribute */
	    if (lablen)
               ds += lablen;
            else
               ds += s[ds]+1;
	 }
   }
while (opcode);
}

/***************************************************************
*
*  Convert1to2 - Convert OMF 1.0 segment to OMF 2.0 format
*
*  Inputs:
*	len - length of the 1.0 segment
*	s - pointer to OMF 1.0 segment
*
*  Outputs:
*	s - pointer to new OMF 2.0 segment
*
*  Returns: length of the new segment
*
***************************************************************/

long Convert1to2 (long len)

{
long dispdata;				/* disp to the data */
int i;					/* loop variable */
int kind;				/* kind field */
int flen;				/* length of a field */
int lablen;				/* length of labels */
char name[MAXNAME];			/* used to read names from the file */
int numlen;				/* length of numbers */
int opcode;				/* current instruction */
BOOL sex;				/* numsex */

lablen = s[13];				/* get length of fields */
numlen = s[14];
sex = s[32];	 			/* read dispdata */
if (sex)
   dispdata = (s[42] << 8) + s[43];
else
   dispdata = s[42] + (s[43] << 8);

ds = dispdata;				/* scan the segment for fields     */
do {					/* that must be expanded           */
   opcode = s[ds++];
   switch (opcode) {
      case   0:				/* $00 END */
      	 break;
      case 224:				/* $E0 ALIGN */
      case 225:				/* $E1 ORG */
      case 241:				/* $F1 DS */
	 ds = ds+numlen;
         break;
      case 226:				/* $E2 RELOC */
      case 227:				/* $E3 INTERSEG */
      case 233:				/* $E9 unused */
      case 234:				/* $EA unused */
      case 255: case 244: case 245: case 246:
      case 247: case 248: case 249: case 250:
      case 251: case 252: case 253: case 254:
	 Error(6, -1, 0);		/* object module format error */
	 return -1;
      case 228:				/* $E4 USING */
      case 229:				/* $E5 STRONG */
	 if (lablen)
            ds += lablen;
         else
            ds += s[ds]+1;
	 break;
      case 230:				/* $E6 GLOBAL */
	 ReadName(name, lablen);
	 ds += 3;
         ++len;
	 break;
      case 231:				/* $E7 GEQU */
	 ReadName(name, lablen);
	 ds += 3;
         ++len;
	 ReadExpression(lablen, numlen);
	 break;
      case 232:				/* $E8 MEM */
	 ds += numlen*2;
	 break;
      case 235:				/* $EB EXPR */
      case 236:				/* $EC ZEXPR */
      case 237:				/* $ED BEXPR */
      case 243:				/* $F3 LEXPR */
	 ++ds;
         ReadExpression(lablen, numlen);
         break;
      case 238:				/* $EE RELEXPR */
	 ds += 1+numlen;
	 ReadExpression(lablen, numlen);
	 break;
      case 239:				/* $EF LOCAL */
         ++len;
	 if (lablen)
            ds += lablen+4;
	 else
            ds += s[ds]+5;
	 break;
      case 240:				/* $F0 EQU */
         ++len;
	 if (lablen)
            ds += lablen+4;
	 else
            ds += s[ds]+5;
	 ReadExpression(lablen, numlen);
	 break;
      case 242:				/* $F2 LCONST */
	 flen = ReadNumber();
	 ds += flen;
	 break;
      default:				/* short constant */
	 ds += opcode;
      }
   }
while (opcode);
s2 = tmalloc(len);			/* get memory for the new segment */
memcpy(s2, s, dispdata);		/* copy in the old header */
if (sex) {				/* set the length of the segment */
   s2[0] = len >> 24;
   s2[1] = (len >> 16) & 255;
   s2[2] = (len >> 8) & 255;
   s2[3] = len & 255;
   }
else {
   s2[0] = len & 255;
   s2[1] = (len >> 8) & 255;
   s2[2] = (len >> 16) & 255;
   s2[3] = len >> 24;
   }
s2[15] = 2;				/* change the version to 2 */
kind = s[12];				/* set the new kind field */
kind = ((kind & 0xE0) << 8) | (kind & 0x1F);
if (sex) {
   s2[20] = kind >> 8;
   s2[21] = kind & 0xFF;
   }
else {
   s2[20] = kind & 0xFF;
   s2[21] = kind >> 8;
   }
s2[12] = 0;
ds = dispdata;				/* move and convert the segment */
nds = ds;
do {
   opcode = s[ds++];
   s2[nds++] = opcode;
   switch (opcode) {
      case   0:				/* $00 END */
      	 break;
      case 224:				/* $E0 ALIGN */
      case 225:				/* $E1 ORG */
      case 241:				/* $F1 DS */
         for (i = 0; i < numlen; ++i)
            s2[nds++] = s[ds++];
         break;
      case 226:				/* $E2 RELOC */
      case 227:				/* $E3 INTERSEG */
      case 233:				/* $E9 unused */
      case 234:				/* $EA unused */
      case 255: case 244: case 245: case 246:
      case 247: case 248: case 249: case 250:
      case 251: case 252: case 253: case 254:
	 Error(6, -1, 0);		/* object module format error */
	 return -1;
      case 228:				/* $E4 USING */
      case 229:				/* $E5 STRONG */
	 CopyName(lablen);
	 break;
      case 230:				/* $E6 GLOBAL */
      case 239:				/* $EF LOCAL */
	 CopyName(lablen);
         s2[nds++] = s[ds++];
         s2[nds++] = 0;
         s2[nds++] = s[ds++];
         s2[nds++] = s[ds++];
	 break;
      case 231:				/* $E7 GEQU */
      case 240:				/* $F0 EQU */
	 CopyName(lablen);
         s2[nds++] = s[ds++];
         s2[nds++] = 0;
         s2[nds++] = s[ds++];
         s2[nds++] = s[ds++];
	 CopyExpression(lablen, numlen);
	 break;
      case 232:				/* $E8 MEM */
         for (i = 0; i < numlen*2; ++i)
            s2[nds++] = s[ds++];
	 break;
      case 235:				/* $EB EXPR */
      case 236:				/* $EC ZEXPR */
      case 237:				/* $ED BEXPR */
      case 243:				/* $F3 LEXPR */
         s2[nds++] = s[ds++];
         CopyExpression(lablen, numlen);
         break;
      case 238:				/* $EE RELEXPR */
         for (i = 0; i < numlen+1; ++i)
            s2[nds++] = s[ds++];
	 CopyExpression(lablen, numlen);
	 break;
      case 242:				/* $F2 LCONST */
	 flen = CopyNumber();
         memcpy(s2+nds, s+ds, flen);
         nds += flen;
	 ds += flen;
	 break;
      default:				/* short constant */
         memcpy(s2+nds, s+ds, opcode);
	 nds += opcode;
	 ds += opcode;
      }
   }
while (opcode);
free(s);				/* use the new segment */
s = s2;
s2 = NULL;
return len;				/* return the new length */
}

/***************************************************************
*
*  ExpandDev - expand devices
*
*  Parameters:
*       name - pointer to the name buffer
*
***************************************************************/

void ExpandDev (char (*name)[])

{
struct rec {				/* expand path record */
   int pCount;
   osNamePtr in;
   outNamePtr out;
   } exRec;

exRec.out = tmalloc(MAXFILE+5);
exRec.in = tmalloc(MAXFILE+3);
exRec.out->buffsize = MAXFILE+4;
exRec.pCount = 2;
exRec.in->length = strlen(*name);
strcpy(exRec.in->str, *name);
EXPAND_DEVICES((void *) &exRec);
exRec.out->str[exRec.out->length] = 0;
strcpy(*name, exRec.out->str);
free(exRec.out);
free(exRec.in);
}

/***************************************************************
*
*  NextCh - Get the next character from the input line
*
*  Inputs:
*	line - input line
*
*  Outputs:
*	line - line with first char removed
*
*  Returns:
*	Character from start of the line
*
***************************************************************/

char NextCh (void)

{
char ch;
int i;

if (ch = line[0])
   for (i = 0; i < (MAXLINE-1); ++i)
      line[i] = line[i+1];
return ch;
}

/***************************************************************
*
*  GetDiskSeg - Read a segment from disk
*
*  Inputs:
*	blockalligned - true if file is a lib file, else false
*
*  Outputs:
*	s - points to segment
*	GetDiskSeg - length of segment if segment was read, else false
*
***************************************************************/

long GetDiskSeg (BOOL blockalligned)

{
long len;
Byte header[42];

if (s != NULL)				/* free any old segment memory */
   free(s);
if (fread(header, 1, 42, inFile) != 42) /* read the header */
   return FALSE;
if (header[32])				/* find the length of the segment */
   len = header[3] | (header[2]<<8) | (((long) header[1])<<16)
      | (((long) header[0])<<24);
else
   len = header[0] | (header[1]<<8) | (((long) header[2])<<16)
      | (((long) header[3])<<24);
if (blockalligned && (header[15] == 1))
   len = len<<9;
s = tmalloc(len);			/* get memory for the segment */
memcpy(s, header, 42);			/* copy in the header */
fread(&s[42], 1, len-42, inFile);	/* read the rest of the segment */
if (header[15] == 1)			/* convert OMF 1.0 to OMF 2.0 */
   len = Convert1to2(len);
return len;
}

/***************************************************************
*
*  GetFlag - Get a flag
*
*  Inputs:
*	argcnt - number of argv's processed so far
*	line - left overs from input line
*
*  Returns:
*	GetFlag - character if flag read, else 0
*
***************************************************************/

char GetFlag (void)

{
char *cptr;
int i;

if (argcnt < largc) {
   cptr = largv[argcnt];
   if ((cptr[0] == '-') && (cptr[2] == 0) && isalpha(cptr[1])) {
      ++argcnt;
      return cptr[1];
      }
   }
else {
   while (isspace(line[0]))
      NextCh();
   if ((line[0] == '-') && (isalpha(line[1])) &&
      ((line[2] == ' ') || (line[2] == 0))) {
      NextCh();
      return NextCh();
      }
   }
return (char) 0;
}

/***************************************************************
*
*  GetFType - get the file type
*
*  Inputs:
*       file - name of the file
*
*  Returns:
*       File type; 0 if there is no file.
*
***************************************************************/

int GetFType (char *file)

{
FileInfoRecGS giRec;			/* GetFileInfo record */

giRec.pCount = 3;
giRec.pathname = tmalloc(strlen(file)+3);
giRec.pathname->length = strlen(file);
strcpy(giRec.pathname->text, file);
giRec.fileType = 0;
GetFileInfoGS((void *) &giRec);
free(giRec.pathname);
return giRec.fileType;
}

/***************************************************************
*
*  GetName - Get a file name
*
*  Inputs:
*	prompt - prompt string
*	argcnt - number of argv's processed so far
*	line - left overs from input line
*
*  Returns:
*	GetName - true if name read, else false
*
*  Outputs:
*	name - name read
*
***************************************************************/

BOOL GetName (char *prompt, char (*name)[])

{
int i;

if (argcnt < largc)
   strcpy(*name, largv[argcnt++]);
else {
   while (isspace(line[0])) NextCh();
   if (!line[0]) {
      printf(prompt);
      fgets(line, MAXLINE, stdin);
      }
   while (isspace(line[0])) NextCh();
   i = 0;
   while ((line[0]) && (!isspace(line[0])))
      (*name)[i++] = NextCh();
   (*name)[i] = (char) 0;
   return i;
   }
return TRUE;
}

/***************************************************************
*
*  GetSegment - Get a segment name
*
*  Inputs:
*	argcnt - number of argv's processed so far
*	line - left overs from input line
*
*  Outputs:
*	segName - name of segment
*	segFlag - processing required:
*		0 - no segment name found
*		'+' - add the segment to the library
*		'-' - delete the segment from the library
*		'^' - remove the segment from the library
*
***************************************************************/

void GetSegment (void)

{
char ch;
char name[MAXLINE];
int i;

name[0] = (char) 0;
segFlag = (char) 0;
if (argcnt < largc)
   strcpy(name, largv[argcnt++]);
else {
   while (isspace(line[0])) NextCh();
   i = 0;
   while ((line[0]) && (!isspace(line[0])))
      name[i++] = NextCh();
   name[i] = 0;
   }
if ((ch = name[0]) && name[1]) {
   if ((ch == '-') || (ch == '+') || (ch == '^')) {
      segFlag = ch;
      strcpy(*segName, &name[1]);
      ExpandDev(segName);
      }
   else if (ch != '\n') {
      Error(9, -1, name);
      exit(-1);
      }
   }
}

/***************************************************************
*
*  Insert - alphabetically insert an entry in a linked list
*
*  Inputs:
*	e - entry to insert
*	l - pointer to head of list
*
*  Outputs:
*	Insert - pointer to new head of list
*
*  Notes:
*	Assumes that the entries in the list start with a pointer
*	to the next element and are followed by a pointer to the
*	name of the entry.
*
***************************************************************/

fileStruct * Insert (fileStruct *e, fileStruct *l)

{
fileStruct *p1,*p2;

if (l) {
   if (strcmp(e->fName, l->fName) < 0) {
      e->fNext = l;
      return e;
      }
   p1 = l;
   p2 = l->fNext;
   while (p2) {
      if (strcmp(e->fName, p2->fName) < 0) {
	 p1->fNext = e;
	 e->fNext = p2;
	 return l;
	 }
      p1 = p2;
      p2 = p2->fNext;
      }
   p1->fNext = e;
   e->fNext = NULL;
   return l;
   }
else {
   e->fNext = NULL;
   return e;
   }
}

/***************************************************************
*
*  ReadDictionary - read an existing library's dictionary
*
*  Inputs:
*	libName - name of the library
*
*  Outputs:
*	file - file name table
*	symbol - symbol table
*	ReadDictionary - true if successful or no library
*		exists, else false
*
***************************************************************/

BOOL ReadDictionary (void)

{
char fn[MAXNAME];			/* used to read names from the file */
long len,num,sds,nds;
fileStruct *f;				/* used for new files */
symbolStruct *sm;			/* used for new symbols */
int type;

type = GetFType(*libName);
if ((type != 178) && (type != 0)) {
   Error(16,-1,*libName);		/* file exists but is not a lib file */
   exit(-1);
   }
if (inFile = fopen(*libName,"rb")) {
   if (GetDiskSeg(FALSE)) {
      if (s[32])
         ds = (s[42]<<8)+s[43];
      else
         ds = s[42]+(s[43]<<8);
      ++ds;
      len = ReadNumber();
      while (len) {
         Spin();
         f = tmalloc(sizeof(fileStruct));
         f->fFile = Read2();
         len = len-3-s[ds];
         ReadName(fn, 0);
         f->fName = tmalloc(strlen(fn)+1);
         strcpy(f->fName, fn);
         file = Insert(f, file);
         }
      ++ds;
      len = ReadNumber();
      sds = ds;
      nds = ds+len+5;
      while (len) {
         Spin();
         len = len-12;
         sm = tmalloc(sizeof(symbolStruct));
         ds = sds;
         ds = nds+Read4();
         sds += 4;
         ReadName(fn, 0);
         sm->sName = tmalloc(strlen(fn)+1);
         strcpy(sm->sName, fn);
         ds = sds;
         sm->sFile = Read2();
         sm->sPrivate = Read2();
         sm->sSeg = Read4();
         symbol = (symbolStruct *) Insert((fileStruct *) sm, (fileStruct *) symbol);
         sds += 8;
         }
      }
   else {
      StopSpin();
      return FALSE;
      }
   fclose(inFile);
   }
StopSpin();
return TRUE;
}

/***************************************************************
*
*  Init - initialize for a file find
*
*  Outputs:
*	inFile - input file
*	outFile - output file
*
*  Returns:
*	Init - TRUE if initialization was successful, else FALSE
*
***************************************************************/

BOOL Init (void)

{
int ind,i;
char ch;

status = 0;				/* no errors found */
symbol = NULL;				/* no symbols, yet */
file = NULL;				/* no files, yet */
s = NULL;				/* no segment memory in use */
line[0] = (char) 0;			/* nothing in the input line */
printDictionary = FALSE;		/* don't print the dictionary */
printFiles = FALSE;			/* don't print the files */
progress = TRUE;			/* print progress information */
argcnt = 1;				/* start with the first argument */
libName = tmalloc(MAXFILE);		/* get memory for file name buffers */
segName = tmalloc(MAXFILE);

while (ch = toupper(GetFlag())) {	/* read the flags */
   if (ch == 'F')
      printFiles = TRUE;
   else if (ch == 'D')
      printDictionary = TRUE;
   else if (ch == 'P')
      progress = FALSE;
   else {
      fprintf(stderr, "%c is not a valid flag.\n", ch);
      exit(-1);
      }
   }
if (progress)
   printf("MakeLib 2.0\n\n");
if (!GetName("Input file name: ", libName)) /* get an input file */
   return FALSE;
ExpandDev(libName);			/* expand devices */
GetSegment();

if ((!segFlag) && (!printDictionary) && (!printFiles)) {
   Error(10,-1,0);			/* no action requested */
   exit(-1);
   }

ReadDictionary();			/* read existing library's dictionary */
return TRUE;
}

/* MakeLib and support routines *******************************/

/***************************************************************
*
*  LibNum - look up library number associated with a symbol
*
*  Inputs:
*	name - name of symbol to look up
*	symbol - head of symbol table list
*
*  Returns:
*	library number; 0 if not found
*
***************************************************************/

int LibNum (char *name)

{
symbolStruct *p;

if (symbol) {
   p = symbol;
   while(p) {
      if (strcmp(name, p->sName) == 0)
         return p->sFile;
      p = p->sNext;
      }
   }
return 0;
}

/***************************************************************
*
*  NewName - enter a symbol name into the symbol table
*
*  Inputs:
*	name - name of the symbol
*	seg - location of the segment
*	file - file number
*	private - private flag
*
*  Retruns:
*	TRUE if added, else FALSE
*
***************************************************************/

BOOL NewName (char *name, long seg, int file, BOOL private)

{
symbolStruct *f, *s;

f = tmalloc(sizeof(symbolStruct));
f->sName = tmalloc(strlen(name)+1);
strcpy(f->sName, name);
f->sFile = file;
f->sPrivate = private;
f->sSeg = seg;
if (symbol) {
   s = symbol;
   while (s) {
      if (!strcmp(name, s->sName)) {
         if (((! private) && (! s->sPrivate)) ||
	    ((private && s->sPrivate) && (file == s->sFile))) {
	    Error(13, -1, name);	/* already in symbol table */
	    return FALSE;
	    }
         }
      s = s->sNext;
      }
   }
symbol = (symbolStruct *) Insert((fileStruct *) f, (fileStruct *) symbol);
return TRUE;
}

/***************************************************************
*
*  AddSeg - add a segment to the dictionary and output file
*
*  Inputs:
*	inFile - file to get segment from
*	outFile - file to write segment to
*	fnum - file number
*
*  Returns:
*	1 - more segments left
*	0 - all segments processed
*	-1 - read error
*
***************************************************************/

int AddSeg (long fnum)

{
long seg,private;
long dispname,dispdata,len;
int lablen,numlen,sex,opcode;
char name[MAXNAME];
int i;

CheckESC();				/* check for early termination */
seg = ftell(outFile);			/* set disp to the segment */
if ((i = GetDiskSeg(TRUE)) <= 0) return i; /* read segment;
					      return if none left */
private = (s[21] & 0x40) != 0;		/* set private flag */
lablen = s[13];				/* get length of fields */
numlen = s[14];
if (s[15] != 2) {			/* make sure its the right version */
   Error(5, -1, 0);			/* unsupported version */
   return FALSE;
   }
sex = s[32];	 			/* read dispname */
if (sex)
   dispname = (s[40] << 8) + s[41];
else
   dispname = s[40] + (s[41] << 8);
if (sex) 				/* read dispdata */
   dispdata = (s[42] << 8) + s[43];
else
   dispdata = s[42] + (s[43] << 8);

ds = dispname+10;			/* put the segment name in the table */
ReadName(name, lablen);
if (!NewName(name, seg, fnum, private))
   return -1;

ds = dispdata;				/* scan the segment for references */
do {
   opcode = s[ds++];
   switch (opcode) {
      case   0:				/* $00 END */
      	 break;
      case 224:				/* $E0 ALIGN */
      case 225:				/* $E1 ORG */
      case 241:				/* $F1 DS */
	 ds = ds+numlen;
         break;
      case 226:				/* $E2 RELOC */
      case 227:				/* $E3 INTERSEG */
      case 233:				/* $E9 unused */
      case 234:				/* $EA unused */
      case 255: case 244: case 245: case 246:
      case 247: case 248: case 249: case 250:
      case 251: case 252: case 253: case 254:
	 Error(6, -1, 0);		/* object module format error */
	 return -1;
      case 228:				/* $E4 USING */
      case 229:				/* $E5 STRONG */
	 if (lablen)
            ds += lablen;
         else
            ds += s[ds]+1;
	 break;
      case 230:				/* $E6 GLOBAL */
	 ReadName(name, lablen);
	 ds += 3;
	 private = s[ds++];
	 if (!NewName(name, seg, fnum, private))
            return -1;
	 break;
      case 231:				/* $E7 GEQU */
	 ReadName(name, lablen);
	 ds += 3;
	 private = s[ds++];
	 if (!NewName(name, seg, fnum, private))
            return -1;
	 ReadExpression(lablen, numlen);
	 break;
      case 232:				/* $E8 MEM */
	 ds += numlen*2;
	 break;
      case 235:				/* $EB EXPR */
      case 236:				/* $EC ZEXPR */
      case 237:				/* $ED BEXPR */
      case 243:				/* $F3 LEXPR */
	 ++ds;
         ReadExpression(lablen, numlen);
         break;
      case 238:				/* $EE RELEXPR */
	 ds += 1+numlen;
	 ReadExpression(lablen, numlen);
	 break;
      case 239:				/* $EF LOCAL */
	 if (lablen)
            ds += lablen+4;
	 else
            ds += s[ds]+5;
	 break;
      case 240:				/* $F0 EQU */
	 if (lablen)
            ds += lablen+4;
	 else
            ds += s[ds]+5;
	 ReadExpression(lablen, numlen);
	 break;
      case 242:				/* $F2 LCONST */
	 len = ReadNumber();
	 ds += len;
	 break;
      default:				/* short constant */
	 ds += opcode;
      }
   }
while (opcode);
if (sex) {				/* set the length of the segment */
   s[0] = ds >> 24;
   s[1] = (ds >> 16) & 255;
   s[2] = (ds >> 8) & 255;
   s[3] = ds & 255;
   }
else {
   s[0] = ds & 255;
   s[1] = (ds >> 8) & 255;
   s[2] = (ds >> 16) & 255;
   s[3] = ds >> 24;
   }
if (!(ds == fwrite(s, 1, ds, outFile)))	/* write the seg to the work file */
   {					/* error writing to work file */
   Error(7, -1, 0);
   exit(-1);
   }
free(s); 				/* get rid of the segment */
s = NULL;
return TRUE;
}

/***************************************************************
*
*  CopyLib - copy segments from the existing library to the work file
*
*  Inputs:
*	outFile - opened work file
*	objFile - opened obj file to put removed segments in;
*		0 if segments are to be disposed of
*	libName - name of library file
*	dfile - number of file who's entries are to be deleted;
*		0 if all are to remain
*
***************************************************************/

void CopyLib (int dfile)

{
long len,i;
symbolStruct *p;
char name[MAXNAME];

if (inFile = fopen(*libName, "rb")) {	/* open the library */
   Spin();
   i = GetDiskSeg(FALSE);		/* read and throw away dictionary */
   free(s);
   s = NULL;
   if (symbol) {			/* adjust disps for library files */
      p = symbol;
      i = ftell(outFile)-i;
      while (p) {
	 if (p->sFile != addFNum)
            p->sSeg = p->sSeg+i;
	 p = p->sNext;
	 }
      }
   while ((len = GetDiskSeg(FALSE)) > 0) /* process the segments */
      {
      Spin();
      if (s[32])			/*   read name of segment */
	 ds = (s[40]<<8) + s[41];
      else
	 ds = s[40] + (s[41]<<8);
      ds += 10;
      ReadName(name, s[13]);
      if (LibNum(name) == dfile) {	/*   if its in the dfile, delete it */
	 if (objFile) {
	    i = (len+511)/512;
	    if (s[32]) {
	       s[0] = i>>24; s[1] = i>>16; s[2] = i>>8; s[3] = i;
               }
	    else {
	       s[3] = i>>24; s[2] = i>>16; s[1] = i>>8; s[0] = i;
               }
	    fwrite(s, 1, i*512, objFile);
	    }
	 if (symbol) {			/*     adjust disps for library files */
            p = symbol;
	    i = ftell(outFile);
	    while (p) {
	       if (p->sFile != addFNum)
		  if (p->sSeg >= i)
                     p->sSeg = p->sSeg-len;
	       p = p->sNext;
	       }
	    }
	 }
      else 				/*   otherwise, place in work file */
	 fwrite(s, 1, len, outFile);
      free(s);
      s = NULL;
      }
   fclose(inFile);			/* close the library */
   }
}

/***************************************************************
*
*  Delete - delete entries assiciated with a file
*
*  Inputs:
*	num - file number of entries to delete
*	list - list to extract entries from
*
*  Returns:
*	head of new list
*
*  Notes:
*	Assumes that the entries in the list start with a pointer
*	to the next element, are followed by a pointer to the
*	name of the entry, and then by the file number.
*
***************************************************************/

fileStruct * Delete (int num, fileStruct *list)

{
fileStruct *f,*f2;

while ((list != NULL) && (list->fFile == num)) {
   f = list;
   list = f->fNext;
   free(f);
   }
if (list) {
   f2 = list;
   f = f2->fNext;
   while (f) {
      if (f->fFile == num) {
	 f2->fNext = f->fNext;
	 free(f);
	 f = f2->fNext;
	 }
      else {
	 f2 = f;
         f = f->fNext;
         }
      }
   }
return list;
}

/***************************************************************
*
*  FileExists - see if a file is in the object table
*
*  Inputs:
*	fn - file name
*
*  Returns:
*	TRUE if in table, else FALSE
*
***************************************************************/

BOOL FileExists (char *fn)

{
fileStruct *f;

if (file) {
   f = file;
   while (f) {
      if (strcmp(fn, f->fName) == 0)
         return TRUE;
      f = f->fNext;
      }
   }
return FALSE;
}

/***************************************************************
*
*  FileName - extract the file name from a path name
*
*  Inputs:
*	f2 - path name
*
*  Outputs:
*	f1 - file name
*
***************************************************************/

void FileName (char *f1, char *f2)

{
int i,j;
char ch, sep;

sep = ':';				/* determine what the separator is */
i = 0;
while (ch = f2[i++])
   if ((ch == '/') || (ch == ':')) {
      sep = ch;
      goto out;
      }
out:
i = j = 0;				/* scan for the last separator */
while (ch = f2[j++])
   if (ch == sep)
      i = j;
j = 0;
do {					/* form the file name */
   ch = f1[j] = toupper(f2[i]);
   ++j; ++i;
   }
while (ch);
f1[j] = 0;
}

/***************************************************************
*
*  FileNumber - find the number of a file
*
*  Inputs:
*	name - name of the file to find
*
*  Returns:
*	0 if no file found, else file number
*
***************************************************************/

int FileNumber (char *name)

{
fileStruct *f;
int i;
char name2[MAXNAME];

if (file) {
   f = file;
   while (f) {
      strcpy(name2, f->fName);
      i = 0;
      while (name2[i]) {
         name2[i] = toupper(name2[i]);
         ++i;
      }
      if (! strcmp(name2, name))
         return f->fFile;
      f = f->fNext;
      }
   }
return FALSE;
}

/***************************************************************
*
*  AddObj - add an object module
*
*  Inputs:
*	segName - name of segment to add
*
*  Returns:
*	TRUE if successful, else FALSE
*
***************************************************************/

BOOL AddObj (void)

{
char fn[MAXNAME];
fileStruct *f;
int fnum,err;

CheckESC();				/* check for early termination */
FileName(fn, *segName);
if (progress)
   printf("Adding %s\n", fn);
if (FileExists(fn)) {
   Error(1, -1, fn);
   return FALSE;
   }
if (GetFType(*segName) != 177) {	/* make sure its an object module */
   Error(3, -1, *segName);		/* file is not a OBJ file */
   return FALSE;
   }
for (fnum = 1; fnum < 32767; ++fnum) {	/* find an unused file number */
   if (file) {
      Spin();
      f = file;
      while (f) {
	 if (f->fFile == fnum)
            goto next;
	 else
            f = f->fNext;
	 }
      }
   goto out;
   next:;
   }
out: addFNum = fnum;
f = tmalloc(sizeof(fileStruct));	/* insert file into file name table */
f->fName = tmalloc(strlen(fn)+1);
strcpy(f->fName, fn);
f->fFile = fnum;
file = Insert(f, file);

if (inFile = fopen(*segName, "rb")) {	/* add the segments to the file */
   while ((err = AddSeg(fnum)) == 1)
      Spin();
   if (err == -1) {
      printf("Removing %s from the dictionary.\n", fn);
      file = Delete(fnum, file);
      symbol = (symbolStruct *) Delete(fnum, (fileStruct *) symbol);
      StopSpin();
      return FALSE;
      }
   fclose(inFile);
   }
else {
   Error(4, -1, *segName);		/* could not open file */
   exit(-1);
   }
objFile = NULL; CopyLib(0); 		/* copy old library segments */
StopSpin();
return TRUE;
}

/***************************************************************
*
*  DeleteObj - delete an object module
*
*  Returns:
*	TRUE if successful, else FALSE
*
***************************************************************/

BOOL DeleteObj (void)

{
char fn[MAXNAME];
int num;

addFNum = 0;
FileName(fn, *segName);
if (progress)
   printf("Deleting %s\n", fn);
if (!(num = FileNumber(fn))) {
   Error(8, -1, fn);			/* object module is not in library */
   StopSpin();
   return FALSE;
   }
objFile = NULL; CopyLib(num);
file = Delete(num, file);
symbol = (symbolStruct *) Delete(num, (fileStruct *) symbol);
StopSpin();
return TRUE;
}

/***************************************************************
*
*  Destroy - delete a disk file
*
*  Inputs:
*       name - name of the file to delete
*
***************************************************************/

void Destroy (char *name)

{
NameRecGS dsRec;			/* GS/OS record */

dsRec.pCount = 1;
dsRec.pathname = tmalloc(strlen(name)+3);
dsRec.pathname->length = strlen(name);
strcpy(dsRec.pathname->text, name);
DestroyGS((void *) &dsRec);
free(dsRec.pathname);
}

/***************************************************************
*
*  fput2 - write a two byte integer to a character file
*
*  Inputs:
*	val - value
*	stream - file to write to
*
***************************************************************/

void fput2 (int val, FILE *stream)

{
fputc(val, stream);
fputc(val>>8, stream);
}

/***************************************************************
*
*  fput4 - write a four byte integer to a character file
*
*  Inputs:
*	val - value
*	stream - file to write to
*
***************************************************************/

void fput4 (long val, FILE *stream)

{
fputc(val, stream); fputc(val>>8, stream);
fputc(val>>16, stream); fputc(val>>32, stream);
}

/***************************************************************
*
*  fputps - write a pascal stylestring to a character file
*
*  Inputs:
*	str - pointer to string
*	stream - file to write to
*
***************************************************************/

void fputps (char *str, FILE *stream)

{
int i;

i = strlen(str);
fputc(i, stream);
while(*str)
   fputc(*(str++), stream);
}

/***************************************************************
*
*  MakeWork - create the name of the work file
*
*  Returns:
*	Pointer to file name
*
***************************************************************/

char * MakeWork (void)

{
return "14/syslib";
}

/***************************************************************
*
*  SetInfo - set the file type and aux type
*
*  Inputs:
*       file - name of the file
*	filetype - file type
*	auxtype - aux type
*
***************************************************************/

void SetInfo (char *file, int filetype, long auxtype)

{
FileInfoRecGS giRec;			/* GetFileInfo record */

giRec.pCount = 4;
giRec.pathname = tmalloc(strlen(file)+3);
giRec.pathname->length = strlen(file);
strcpy(giRec.pathname->text, file);
GetFileInfoGS((void *) &giRec);
giRec.fileType = filetype;
giRec.auxType = auxtype;
SetFileInfoGS((void *) &giRec);
free(giRec.pathname);
}

/***************************************************************
*
*  RemoveObj - remove an object module
*
*  Inputs:
*	segName - name of segment to remove
*
*  Returns:
*	TRUE if successful, else FALSE
*
***************************************************************/

BOOL RemoveObj (void)

{
char fn[MAXNAME];
int num;

addFNum = 0;
FileName(fn, *segName);
if (progress)
   printf("Extracting %s\n", fn);
if (!(num = FileNumber(fn))) {
   Error(8, -1, fn);			/* object file not in library */
   return FALSE;
   }
if (!(objFile = fopen(*segName,"wb"))) {
   Error(14, -1, *segName); 		/* file could not be opened */
   exit(-1);
   }
SetInfo(*segName, 177, 0);
CopyLib(num);
fclose(objFile);
file = Delete(num, file);
symbol = (symbolStruct *) Delete(num, (fileStruct *) symbol);
StopSpin();
return TRUE;
}

/***************************************************************
*
*  Update - update the library
*
*  Inputs:
*	outFile - file with all of the segments
*	libName - name of the library
*
*  Returns:
*	TRUE if successful, else FALSE
*
***************************************************************/

BOOL Update (void)

{
char *name = "LIBRARY";
long filesize,symbolsize,namesize,len,disp,fslen;
fileStruct *fp;
symbolStruct *sp;
int i;

fclose(outFile); 			/* close the work file */
if (!(outFile = fopen(*libName,"wb"))) { /* open the library */
   Error(15,-1,*libName); 		/* could not open the library */
   exit(-1);
   }

filesize = 0;				/* compute length of file name table */
if (file) {
   fp = file;
   Spin();
   while (fp) {
      filesize += 3+strlen(fp->fName);
      fp = fp->fNext;
      }
   }
symbolsize = namesize = 0;		/* compute length of symbols & names */
if (symbol) {
   sp = symbol;
   Spin();
   while (sp) {
      namesize += 1+strlen(sp->sName);
      symbolsize += 12;
      sp = sp->sNext;
      }
   }
					/* write the header */
fslen = 71+strlen(name)+namesize+symbolsize+filesize;
fput4(fslen, outFile);			/* BLKCNT */
for (i = 0; i < 8; ++i)			/* RESSPC,LENGTH */
   fputc(0, outFile);
fputc(0, outFile);			/* UNDEFINED */
fputc(0, outFile);			/* LABLEN */
fputc(4, outFile);			/* NUMLEN */
fputc(2, outFile);			/* VERSION */
fput4(0, outFile);			/* BANKSIZE */
fput2(8, outFile);			/* KIND */
fput2(0, outFile);			/* UNDEFINED */
fput4(0, outFile);			/* ORG */
fput4(0, outFile);			/* ALIGN */
fput4(0, outFile);			/* NUMSEX, SEGNUM */
fput4(0, outFile);			/* ENTRY */
fputc(44, outFile); fputc(0, outFile);	/* DISPNAME */
i = 44+11+strlen(name);			/* DISPDATA */
fput2(i, outFile);
for (i = 0; i < 10; ++i)		/* LOADNAME */
   fputc(0, outFile);
fputps(name, outFile);			/* SEGNAME */

fputc(242, outFile);			/* write the file names */
fput4(filesize, outFile);
if (file) {
   fp = file;
   while (fp) {
      Spin();
      fput2(fp->fFile, outFile);
      fputps(fp->fName, outFile);
      fp = fp->fNext;
      }
   }
fputc(242, outFile);			/* write symbol table */
fput4(symbolsize, outFile);
if (symbol) {
   disp = 0;
   sp = symbol;
   while (sp) {
      Spin();
      fput4(disp, outFile);
      fput2(sp->sFile, outFile);
      fput2(sp->sPrivate, outFile);
      sp->sSeg = sp->sSeg+fslen;
      fput4(sp->sSeg, outFile);
      disp += strlen(sp->sName)+1;
      sp = sp->sNext;
      }
   }
fputc(242, outFile);			/* write the name table */
fput4(namesize, outFile);
if (symbol) {
   sp = symbol;
   while (sp) {
      Spin();
      fputps(sp->sName, outFile);
      sp = sp->sNext;
      }
   }
fputc(0, outFile);			/* end of segment marker */

inFile = fopen(MakeWork(),"rb");	/* copy work file segments to output */
len = GetDiskSeg(FALSE);
while (len != 0) {
   Spin();
   fwrite(s, 1, len, outFile);
   len = GetDiskSeg(FALSE);
   }
fclose(outFile); 			/* close files */
fclose(inFile);
SetInfo(*libName, 178, 0);		/* set file type of library file */
Destroy(MakeWork());			/* get rid of the work file */
return TRUE;
}

/***************************************************************
*
*  WriteDictionary - write the symbol table
*
*  Inputs:
*	symbol - pointer to top of symbol table
*
***************************************************************/

void WriteDictionary (void)

{
symbolStruct *f;

if (printDictionary) {
   if (symbol) {
      printf("\nSymbols in library:\n"
             "        file      disp in   private   symbol\n"
             "        number    segment   flag      name\n"
             "        ------    -------   -------   ------\n");
      f = symbol;
      while (f) {
         printf("%14d%11ld   ", f->sFile, f->sSeg);
	 if (f->sPrivate)
            printf("true      ");
         else
            printf("false     ");
	 printf("%s\n", f->sName);
	 f = f->sNext;
	 CheckESC();
	 }
      }
   putchar('\n');
   }
}

/***************************************************************
*
*  WriteFiles - write the file name table
*
*  Inputs:
*	file - pointer to top of files list
*
***************************************************************/

void WriteFiles (void)

{
fileStruct *f;

if (printFiles) {
   if (file) {
      printf("\nFiles in library:\n"
             "        number  name\n"
             "        ------  ----\n");
      f = file;
      while (f) {
         printf("%12d    %s\n", f->fFile, f->fName);
	 f = f->fNext;
	 CheckESC();
	 }
      }
   putchar('\n');
   }
}

/***************************************************************
*
*  MakeLib - Create the library file
*
*  Inputs:
*	inFile - input file
*
***************************************************************/

void MakeLib (void)

{
while (segFlag) {
   if (!(outFile = fopen(MakeWork(),"wb"))) {
      Error(11,-1,MakeWork());		/* could not open the work file */
      exit(-1);
      }
   if (segFlag == '+') {
      if (!AddObj()) break;
      }
   else if (segFlag == '-') {
      if (!DeleteObj()) break;
      }
   else if (segFlag == '^') {
      if (!RemoveObj()) break;
      }
   else {
      Error(12,-1,*segName);		/* illegal operation */
      exit(-1);
      }
   Update();
   StopSpin();
   GetSegment();
   }
WriteFiles();
WriteDictionary();
fclose(outFile);			/* close the work file files */
}

/***************************************************************
*
*  main - main program
*
***************************************************************/

int main (int argc, char *argv[])

{
largc = argc;				/* save the command line values */
largv = argv;
if (Init())				/* initialize the program */
   MakeLib();				/* do the work */
exit(status);				/* return the results */
}
