/*
 * Unit test suite for file functions
 *
 * Copyright 2002 Bill Currie
 * Copyright 2005 Paul Rupe
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "wine/test.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <share.h>
#include <sys/stat.h>
#include <io.h>
#include <direct.h>
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <process.h>
#include <errno.h>

static HANDLE proc_handles[2];

static int (__cdecl *p_fopen_s)(FILE**, const char*, const char*);
static int (__cdecl *p__wfopen_s)(FILE**, const wchar_t*, const wchar_t*);

static void init(void)
{
    HMODULE hmod = GetModuleHandleA("msvcrt.dll");

    p_fopen_s = (void*)GetProcAddress(hmod, "fopen_s");
    p__wfopen_s = (void*)GetProcAddress(hmod, "_wfopen_s");
}

static void test_filbuf( void )
{
    FILE *fp;
    int c;
    fpos_t pos;

    fp = fopen("filbuf.tst", "wb");
    fwrite("\n\n\n\n", 1, 4, fp);
    fclose(fp);

    fp = fopen("filbuf.tst", "rt");
    c = _filbuf(fp);
    ok(c == '\n', "read wrong byte\n");
    /* See bug 16970 for why we care about _filbuf.
     * ftell returns screwy values on files with lots
     * of bare LFs in ascii mode because it assumes
     * that ascii files contain only CRLFs, removes
     * the CR's early in _filbuf, and adjusts the return
     * value of ftell to compensate.
     * native _filbuf will read the whole file, then consume and return
     * the first one.  That leaves fp->_fd at offset 4, and fp->_ptr
     * pointing to a buffer of three bare LFs, so
     * ftell will return 4 - 3 - 3 = -2.
     */
    ok(ftell(fp) == -2, "ascii crlf removal does not match native\n");
    ok(fgetpos(fp, &pos) == 0, "fgetpos fail\n");
    ok(pos == -2, "ftell does not match fgetpos\n");
    fclose(fp);
    unlink("filbuf.tst");
}

static void test_fdopen( void )
{
    static const char buffer[] = {0,1,2,3,4,5,6,7,8,9};
    char ibuf[10];
    int fd;
    FILE *file;

    fd = open ("fdopen.tst", O_WRONLY | O_CREAT | O_BINARY, _S_IREAD |_S_IWRITE);
    write (fd, buffer, sizeof (buffer));
    close (fd);

    fd = open ("fdopen.tst", O_RDONLY | O_BINARY);
    lseek (fd, 5, SEEK_SET);
    file = fdopen (fd, "rb");
    ok (fread (ibuf, 1, sizeof (buffer), file) == 5, "read wrong byte count\n");
    ok (memcmp (ibuf, buffer + 5, 5) == 0, "read wrong bytes\n");
    fclose (file);
    unlink ("fdopen.tst");
}

static void test_fileops( void )
{
    static const char outbuffer[] = "0,1,2,3,4,5,6,7,8,9";
    char buffer[256];
    WCHAR wbuffer[256];
    int fd;
    FILE *file;
    fpos_t pos;
    int i, c;

    fd = open ("fdopen.tst", O_WRONLY | O_CREAT | O_BINARY, _S_IREAD |_S_IWRITE);
    write (fd, outbuffer, sizeof (outbuffer));
    close (fd);

    fd = open ("fdopen.tst", O_RDONLY | O_BINARY);
    file = fdopen (fd, "rb");
    ok(strlen(outbuffer) == (sizeof(outbuffer)-1),"strlen/sizeof error\n");
    ok(fgets(buffer,sizeof(buffer),file) !=0,"fgets failed unexpected\n");
    ok(fgets(buffer,sizeof(buffer),file) ==0,"fgets didn't signal EOF\n");
    ok(feof(file) !=0,"feof doesn't signal EOF\n");
    rewind(file);
    ok(fgets(buffer,strlen(outbuffer),file) !=0,"fgets failed unexpected\n");
    ok(lstrlenA(buffer) == lstrlenA(outbuffer) -1,"fgets didn't read right size\n");
    ok(fgets(buffer,sizeof(outbuffer),file) !=0,"fgets failed unexpected\n");
    ok(strlen(buffer) == 1,"fgets dropped chars\n");
    ok(buffer[0] == outbuffer[strlen(outbuffer)-1],"fgets exchanged chars\n");

    rewind(file);
    for (i = 0, c = EOF; i < sizeof(outbuffer); i++)
    {
        ok((c = fgetc(file)) == outbuffer[i], "fgetc returned wrong data\n");
    }
    ok((c = fgetc(file)) == EOF, "getc did not return EOF\n");
    ok(feof(file), "feof did not return EOF\n");
    ok(ungetc(c, file) == EOF, "ungetc(EOF) did not return EOF\n");
    ok(feof(file), "feof after ungetc(EOF) did not return EOF\n");
    ok((c = fgetc(file)) == EOF, "getc did not return EOF\n");
    c = outbuffer[sizeof(outbuffer) - 1];
    ok(ungetc(c, file) == c, "ungetc did not return its input\n");
    ok(!feof(file), "feof after ungetc returned EOF\n");
    ok((c = fgetc(file)) != EOF, "getc after ungetc returned EOF\n");
    ok(c == outbuffer[sizeof(outbuffer) - 1],
       "getc did not return ungetc'd data\n");
    ok(!feof(file), "feof after getc returned EOF prematurely\n");
    ok((c = fgetc(file)) == EOF, "getc did not return EOF\n");
    ok(feof(file), "feof after getc did not return EOF\n");

    rewind(file);
    ok(fgetpos(file,&pos) == 0, "fgetpos failed unexpected\n");
    ok(pos == 0, "Unexpected result of fgetpos %x%08x\n", (DWORD)(pos >> 32), (DWORD)pos);
    pos = sizeof (outbuffer);
    ok(fsetpos(file, &pos) == 0, "fsetpos failed unexpected\n");
    ok(fgetpos(file,&pos) == 0, "fgetpos failed unexpected\n");
    ok(pos == sizeof (outbuffer), "Unexpected result of fgetpos %x%08x\n", (DWORD)(pos >> 32), (DWORD)pos);

    fclose (file);
    fd = open ("fdopen.tst", O_RDONLY | O_TEXT);
    file = fdopen (fd, "rt"); /* open in TEXT mode */
    ok(fgetws(wbuffer,sizeof(wbuffer)/sizeof(wbuffer[0]),file) !=0,"fgetws failed unexpected\n");
    ok(fgetws(wbuffer,sizeof(wbuffer)/sizeof(wbuffer[0]),file) ==0,"fgetws didn't signal EOF\n");
    ok(feof(file) !=0,"feof doesn't signal EOF\n");
    rewind(file);
    ok(fgetws(wbuffer,strlen(outbuffer),file) !=0,"fgetws failed unexpected\n");
    ok(lstrlenW(wbuffer) == (lstrlenA(outbuffer) -1),"fgetws didn't read right size\n");
    ok(fgetws(wbuffer,sizeof(outbuffer)/sizeof(outbuffer[0]),file) !=0,"fgets failed unexpected\n");
    ok(lstrlenW(wbuffer) == 1,"fgets dropped chars\n");
    fclose (file);

    file = fopen("fdopen.tst", "rb");
    ok( file != NULL, "fopen failed\n");
    /* sizeof(buffer) > content of file */
    ok(fread(buffer, sizeof(buffer), 1, file) == 0, "fread test failed\n");
    /* feof should be set now */
    ok(feof(file), "feof after fread failed\n");
    fclose (file);

    unlink ("fdopen.tst");
}

#define IOMODE (ao?"ascii mode":"binary mode")
static void test_readmode( BOOL ascii_mode )
{
    static const char outbuffer[] = "0,1,2,3,4,5,6,7,8,9\r\n\r\nA,B,C,D,E\r\nX,Y,Z";
    static const char padbuffer[] = "ghjghjghjghj";
    static const char nlbuffer[] = "\r\n";
    char buffer[2*BUFSIZ+256];
    const char *optr;
    int fd;
    FILE *file;
    const int *ip;
    int i, j, m, ao, pl;
    unsigned int fp;
    LONG l;

    fd = open ("fdopen.tst", O_WRONLY | O_CREAT | O_BINARY, _S_IREAD |_S_IWRITE);
    /* an internal buffer of BUFSIZ is maintained, so make a file big
     * enough to test operations that cross the buffer boundary 
     */
    j = (2*BUFSIZ-4)/strlen(padbuffer);
    for (i=0; i<j; i++)
        write (fd, padbuffer, strlen(padbuffer));
    j = (2*BUFSIZ-4)%strlen(padbuffer);
    for (i=0; i<j; i++)
        write (fd, &padbuffer[i], 1);
    write (fd, nlbuffer, strlen(nlbuffer));
    write (fd, outbuffer, sizeof (outbuffer));
    close (fd);
    
    if (ascii_mode) {
        /* Open file in ascii mode */
        fd = open ("fdopen.tst", O_RDONLY);
        file = fdopen (fd, "r");
        ao = -1; /* on offset to account for carriage returns */
    }
    else {
        fd = open ("fdopen.tst", O_RDONLY | O_BINARY);
        file = fdopen (fd, "rb");
        ao = 0;
    }
    
    /* first is a test of fgets, ftell, fseek */
    ok(ftell(file) == 0,"Did not start at beginning of file in %s\n", IOMODE);
    ok(fgets(buffer,2*BUFSIZ+256,file) !=0,"padding line fgets failed unexpected in %s\n", IOMODE);
    l = ftell(file);
    pl = 2*BUFSIZ-2;
    ok(l == pl,"padding line ftell got %d should be %d in %s\n", l, pl, IOMODE);
    ok(lstrlenA(buffer) == pl+ao,"padding line fgets got size %d should be %d in %s\n",
     lstrlenA(buffer), pl+ao, IOMODE);
    for (fp=0; fp<strlen(outbuffer); fp++)
        if (outbuffer[fp] == '\n') break;
    fp++;
    ok(fgets(buffer,256,file) !=0,"line 1 fgets failed unexpected in %s\n", IOMODE);
    l = ftell(file);
    ok(l == pl+fp,"line 1 ftell got %d should be %d in %s\n", l, pl+fp, IOMODE);
    ok(lstrlenA(buffer) == fp+ao,"line 1 fgets got size %d should be %d in %s\n",
     lstrlenA(buffer), fp+ao, IOMODE);
    /* test a seek back across the buffer boundary */
    l = pl;
    ok(fseek(file,l,SEEK_SET)==0,"seek failure in %s\n", IOMODE);
    l = ftell(file);
    ok(l == pl,"ftell after seek got %d should be %d in %s\n", l, pl, IOMODE);
    ok(fgets(buffer,256,file) !=0,"second read of line 1 fgets failed unexpected in %s\n", IOMODE);
    l = ftell(file);
    ok(l == pl+fp,"second read of line 1 ftell got %d should be %d in %s\n", l, pl+fp, IOMODE);
    ok(lstrlenA(buffer) == fp+ao,"second read of line 1 fgets got size %d should be %d in %s\n",
     lstrlenA(buffer), fp+ao, IOMODE);
    ok(fgets(buffer,256,file) !=0,"line 2 fgets failed unexpected in %s\n", IOMODE);
    fp += 2;
    l = ftell(file);
    ok(l == pl+fp,"line 2 ftell got %d should be %d in %s\n", l, pl+fp, IOMODE);
    ok(lstrlenA(buffer) == 2+ao,"line 2 fgets got size %d should be %d in %s\n",
     lstrlenA(buffer), 2+ao, IOMODE);
    
    /* test fread across buffer boundary */
    rewind(file);
    ok(ftell(file) == 0,"Did not start at beginning of file in %s\n", IOMODE);
    ok(fgets(buffer,BUFSIZ-6,file) !=0,"padding line fgets failed unexpected in %s\n", IOMODE);
    j=strlen(outbuffer);
    i=fread(buffer,1,BUFSIZ+strlen(outbuffer),file);
    ok(i==BUFSIZ+j,"fread failed, expected %d got %d in %s\n", BUFSIZ+j, i, IOMODE);
    l = ftell(file);
    ok(l == pl+j-(ao*4)-5,"ftell after fread got %d should be %d in %s\n", l, pl+j-(ao*4)-5, IOMODE);
    for (m=0; m<3; m++)
        ok(buffer[m]==padbuffer[m+(BUFSIZ-4)%strlen(padbuffer)],"expected %c got %c\n", padbuffer[m], buffer[m]);
    m+=BUFSIZ+2+ao;
    optr = outbuffer;
    for (; m<i; m++) {
        ok(buffer[m]==*optr,"char %d expected %c got %c in %s\n", m, *optr, buffer[m], IOMODE);
        optr++;
        if (ao && (*optr == '\r'))
            optr++;
    }
    /* fread should return the requested number of bytes if available */
    rewind(file);
    ok(ftell(file) == 0,"Did not start at beginning of file in %s\n", IOMODE);
    ok(fgets(buffer,BUFSIZ-6,file) !=0,"padding line fgets failed unexpected in %s\n", IOMODE);
    j = fp+10;
    i=fread(buffer,1,j,file);
    ok(i==j,"fread failed, expected %d got %d in %s\n", j, i, IOMODE);
    /* test fread eof */
    ok(fseek(file,0,SEEK_END)==0,"seek failure in %s\n", IOMODE);
    ok(feof(file)==0,"feof failure in %s\n", IOMODE);
    ok(fread(buffer,1,1,file)==0,"fread failure in %s\n", IOMODE);
    ok(feof(file)!=0,"feof failure in %s\n", IOMODE);
    ok(fseek(file,-3,SEEK_CUR)==0,"seek failure in %s\n", IOMODE);
    ok(feof(file)==0,"feof failure in %s\n", IOMODE);
    ok(fread(buffer,2,1,file)==1,"fread failed in %s\n", IOMODE);
    ok(feof(file)==0,"feof failure in %s\n", IOMODE);
    ok(fread(buffer,2,1,file)==0,"fread failure in %s\n",IOMODE);
    ok(feof(file)!=0,"feof failure in %s\n", IOMODE);
    
    /* test some additional functions */
    rewind(file);
    ok(ftell(file) == 0,"Did not start at beginning of file in %s\n", IOMODE);
    ok(fgets(buffer,2*BUFSIZ+256,file) !=0,"padding line fgets failed unexpected in %s\n", IOMODE);
    i = _getw(file);
    ip = (const int *)outbuffer;
    ok(i == *ip,"_getw failed, expected %08x got %08x in %s\n", *ip, i, IOMODE);
    for (fp=0; fp<strlen(outbuffer); fp++)
        if (outbuffer[fp] == '\n') break;
    fp++;
    /* this will cause the next _getw to cross carriage return characters */
    ok(fgets(buffer,fp-6,file) !=0,"line 1 fgets failed unexpected in %s\n", IOMODE);
    for (i=0, j=0; i<6; i++) {
        if (ao==0 || outbuffer[fp-3+i] != '\r')
            buffer[j++] = outbuffer[fp-3+i];
    }
    i = _getw(file);
    ip = (int *)buffer;
    ok(i == *ip,"_getw failed, expected %08x got %08x in %s\n", *ip, i, IOMODE);

    fclose (file);
    unlink ("fdopen.tst");
}

static void test_asciimode(void)
{
    FILE *fp;
    char buf[64];
    int c, i, j;

    /* Simple test of CR CR LF handling.  Test both fgets and fread code paths, they're different! */
    fp = fopen("ascii.tst", "wb");
    fputs("\r\r\n", fp);
    fclose(fp);
    fp = fopen("ascii.tst", "rt");
    ok(fgets(buf, sizeof(buf), fp) != NULL, "fgets\n");
    ok(0 == strcmp(buf, "\r\n"), "CR CR LF not read as CR LF\n");
    rewind(fp);
    ok((fread(buf, 1, sizeof(buf), fp) == 2) && (0 == strcmp(buf, "\r\n")), "CR CR LF not read as CR LF\n");
    fclose(fp);
    unlink("ascii.tst");

    /* Simple test of foo ^Z [more than one block] bar handling */
    fp = fopen("ascii.tst", "wb");
    fputs("foo\032", fp);  /* foo, logical EOF, ... */
    fseek(fp, 65536L, SEEK_SET); /* ... more than MSVCRT_BUFSIZ, ... */
    fputs("bar", fp); /* ... bar */
    fclose(fp);
    fp = fopen("ascii.tst", "rt");
    ok(fgets(buf, sizeof(buf), fp) != NULL, "fgets foo\n");
    ok(0 == strcmp(buf, "foo"), "foo ^Z not read as foo by fgets\n");
    ok(fgets(buf, sizeof(buf), fp) == NULL, "fgets after logical EOF\n");
    rewind(fp);
    ok((fread(buf, 1, sizeof(buf), fp) == 3) && (0 == strcmp(buf, "foo")), "foo ^Z not read as foo by fread\n");
    ok((fread(buf, 1, sizeof(buf), fp) == 0), "fread after logical EOF\n");
    fclose(fp);

    /* Show ASCII mode handling*/
    fp= fopen("ascii.tst","wb");
    fputs("0\r\n1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7\r\n8\r\n9\r\n", fp);
    fclose(fp);

    fp = fopen("ascii.tst", "r");
    c= fgetc(fp);
    c= fgetc(fp);
    fseek(fp,0,SEEK_CUR);
    for(i=1; i<10; i++) {
	ok((j = ftell(fp)) == i*3, "ftell fails in TEXT mode\n");
	fseek(fp,0,SEEK_CUR);
	ok((c = fgetc(fp)) == '0'+ i, "fgetc after fseek failed in line %d\n", i);
	c= fgetc(fp);
    }
    /* Show that fseek doesn't skip \\r !*/
    rewind(fp);
    c= fgetc(fp);
    fseek(fp, 2 ,SEEK_CUR);
    for(i=1; i<10; i++) {
	ok((c = fgetc(fp)) == '0'+ i, "fgetc after fseek with pos Offset failed in line %d\n", i);
	fseek(fp, 2 ,SEEK_CUR);
    }
    fseek(fp, 9*3 ,SEEK_SET);
    c = fgetc(fp);
    fseek(fp, -4 ,SEEK_CUR);
    for(i= 8; i>=0; i--) {
	ok((c = fgetc(fp)) == '0'+ i, "fgetc after fseek with neg Offset failed in line %d\n", i);
	fseek(fp, -4 ,SEEK_CUR);
    }
    /* Show what happens if fseek positions filepointer on \\r */
    fclose(fp);
    fp = fopen("ascii.tst", "r");
    fseek(fp, 3 ,SEEK_SET);
    ok((c = fgetc(fp)) == '1', "fgetc fails to read next char when positioned on \\r\n");
    fclose(fp);

    unlink("ascii.tst");
}

static void test_asciimode2(void)
{
    /* Error sequence from one app was getchar followed by small fread
     * with one \r removed had last byte of buffer filled with
     * next byte of *unbuffered* data rather than next byte from buffer
     * Test case is a short string of one byte followed by a newline
     * followed by filler to fill out the sector, then a sector of
     * some different byte.
     */

    FILE *fp;
    char ibuf[4];
    int i;
    static const char obuf[] =
"00\n"
"000000000000000000000000000000000000000000000000000000000000000000000000000000\n"
"000000000000000000000000000000000000000000000000000000000000000000000000000000\n"
"000000000000000000000000000000000000000000000000000000000000000000000000000000\n"
"000000000000000000000000000000000000000000000000000000000000000000000000000000\n"
"000000000000000000000000000000000000000000000000000000000000000000000000000000\n"
"000000000000000000000000000000000000000000000000000000000000000000000000000000\n"
"000000000000000000\n"
"1111111111111111111";

    fp = fopen("ascii2.tst", "wt");
    fwrite(obuf, 1, sizeof(obuf), fp);
    fclose(fp);

    fp = fopen("ascii2.tst", "rt");
    ok(getc(fp) == '0', "first char not 0\n");
    memset(ibuf, 0, sizeof(ibuf));
    i = fread(ibuf, 1, sizeof(ibuf), fp);
    ok(i == sizeof(ibuf), "fread i %d != sizeof(ibuf)\n", i);
    ok(0 == strncmp(ibuf, obuf+1, sizeof(ibuf)), "ibuf != obuf\n");
    fclose(fp);
    unlink("ascii2.tst");
}

static WCHAR* AtoW( const char* p )
{
    WCHAR* buffer;
    DWORD len = MultiByteToWideChar( CP_ACP, 0, p, -1, NULL, 0 );
    buffer = malloc( len * sizeof(WCHAR) );
    MultiByteToWideChar( CP_ACP, 0, p, -1, buffer, len );
    return buffer;
}

/* Test reading in text mode when the 512'th character read is \r*/
static void test_readboundary(void)
{
  FILE *fp;
  char buf[513], rbuf[513];
  int i, j;
  for (i = 0; i < 511; i++)
    {
      j = (i%('~' - ' ')+ ' ');
      buf[i] = j;
    }
  buf[511] = '\n';
  buf[512] =0;
  fp = fopen("boundary.tst", "wt");
  fwrite(buf, 512,1,fp);
  fclose(fp);
  fp = fopen("boundary.tst", "rt");
  for(i=0; i<512; i++)
    {
      fseek(fp,0 , SEEK_CUR);
      rbuf[i] = fgetc(fp);
    }
  rbuf[512] =0;
  fclose(fp);
  unlink("boundary.tst");

  ok(strcmp(buf, rbuf) == 0,"CRLF on buffer boundary failure\n");
  }

static void test_fgetc( void )
{
  char* tempf;
  FILE *tempfh;
  int  ich=0xe0, ret;

  tempf=_tempnam(".","wne");
  tempfh = fopen(tempf,"w+");
  fputc(ich, tempfh);
  fputc(ich, tempfh);
  rewind(tempfh);
  ret = fgetc(tempfh);
  ok(ich == ret, "First fgetc expected %x got %x\n", ich, ret);
  ret = fgetc(tempfh);
  ok(ich == ret, "Second fgetc expected %x got %x\n", ich, ret);
  fclose(tempfh);
  tempfh = fopen(tempf,"wt");
  fputc('\n', tempfh);
  fclose(tempfh);
  tempfh = fopen(tempf,"wt");
  setbuf(tempfh, NULL);
  ret = fgetc(tempfh);
  ok(ret == -1, "Unbuffered fgetc in text mode must failed on \\r\\n\n");
  fclose(tempfh);
  unlink(tempf);
  free(tempf);
}

static void test_fputc( void )
{
  char* tempf;
  FILE *tempfh;
  int  ret;

  tempf=_tempnam(".","wne");
  tempfh = fopen(tempf,"wb");
  ret = fputc(0,tempfh);
  ok(0 == ret, "fputc(0,tempfh) expected %x got %x\n", 0, ret);
  ret = fputc(0xff,tempfh);
  ok(0xff == ret, "fputc(0xff,tempfh) expected %x got %x\n", 0xff, ret);
  ret = fputc(0xffffffff,tempfh);
  ok(0xff == ret, "fputc(0xffffffff,tempfh) expected %x got %x\n", 0xff, ret);
  fclose(tempfh);

  tempfh = fopen(tempf,"rb");
  ret = fputc(0,tempfh);
  ok(EOF == ret, "fputc(0,tempfh) on r/o file expected %x got %x\n", EOF, ret);
  fclose(tempfh);

  unlink(tempf);
  free(tempf);
}

static void test_flsbuf( void )
{
  char* tempf;
  FILE *tempfh;
  int  c;
  int  ret;
  int  bufmode;
  static const int bufmodes[] = {_IOFBF,_IONBF};

  tempf=_tempnam(".","wne");
  for (bufmode=0; bufmode < sizeof(bufmodes)/sizeof(bufmodes[0]); bufmode++)
  {
    tempfh = fopen(tempf,"wb");
    setvbuf(tempfh,NULL,bufmodes[bufmode],2048);
    ret = _flsbuf(0,tempfh);
    ok(0 == ret, "_flsbuf(0,tempfh) with bufmode %x expected %x got %x\n",
                         bufmodes[bufmode], 0, ret);
    ret = _flsbuf(0xff,tempfh);
    ok(0xff == ret, "_flsbuf(0xff,tempfh) with bufmode %x expected %x got %x\n",
                         bufmodes[bufmode], 0, ret);
    ret = _flsbuf(0xffffffff,tempfh);
    ok(0xff == ret, "_flsbuf(0xffffffff,tempfh) with bufmode %x expected %x got %x\n",
                         bufmodes[bufmode], 0, ret);
    fclose(tempfh);
  }

  tempfh = fopen(tempf,"rb");
  ret = _flsbuf(0,tempfh);
  ok(EOF == ret, "_flsbuf(0,tempfh) on r/o file expected %x got %x\n", EOF, ret);
  fclose(tempfh);

  /* See bug 17123, exposed by WinAVR's make */
  tempfh = fopen(tempf,"w");
  ok(tempfh->_cnt == 0, "_cnt on freshly opened file was %d\n", tempfh->_cnt);
  setbuf(tempfh, NULL);
  ok(tempfh->_cnt == 0, "_cnt on unbuffered file was %d\n", tempfh->_cnt);
  /* Inlined putchar sets _cnt to -1.  Native seems to ignore the value... */
  tempfh->_cnt = 1234;
  ret = _flsbuf('Q',tempfh);
  ok('Q' == ret, "_flsbuf('Q',tempfh) expected %x got %x\n", 'Q', ret);
  /* ... and reset it to zero */
  ok(tempfh->_cnt == 0, "after unbuf _flsbuf, _cnt was %d\n", tempfh->_cnt);
  fclose(tempfh);
  /* And just for grins, make sure the file is correct */
  tempfh = fopen(tempf,"r");
  c = fgetc(tempfh);
  ok(c == 'Q', "first byte should be 'Q'\n");
  c = fgetc(tempfh);
  ok(c == EOF, "there should only be one byte\n");
  fclose(tempfh);

  unlink(tempf);
  free(tempf);
}

static void test_fgetwc( void )
{
#define LLEN 512

  char* tempf;
  FILE *tempfh;
  static const char mytext[]= "This is test_fgetwc\r\n";
  WCHAR wtextW[BUFSIZ+LLEN+1];
  WCHAR *mytextW = NULL, *aptr, *wptr;
  BOOL diff_found = FALSE;
  int j;
  unsigned int i;
  LONG l;

  tempf=_tempnam(".","wne");
  tempfh = fopen(tempf,"wb");
  j = 'a';
  /* pad to almost the length of the internal buffer */
  for (i=0; i<BUFSIZ-4; i++)
    fputc(j,tempfh);
  j = '\r';
  fputc(j,tempfh);
  j = '\n';
  fputc(j,tempfh);
  fputs(mytext,tempfh);
  fclose(tempfh);
  /* in text mode, getws/c expects multibyte characters */
  /*currently Wine only supports plain ascii, and that is all that is tested here */
  tempfh = fopen(tempf,"rt"); /* open in TEXT mode */
  fgetws(wtextW,LLEN,tempfh);
  l=ftell(tempfh);
  ok(l==BUFSIZ-2, "ftell expected %d got %d\n", BUFSIZ-2, l);
  fgetws(wtextW,LLEN,tempfh);
  l=ftell(tempfh);
  ok(l==BUFSIZ-2+strlen(mytext), "ftell expected %d got %d\n", BUFSIZ-2+lstrlen(mytext), l);
  mytextW = AtoW (mytext);
  aptr = mytextW;
  wptr = wtextW;
  for (i=0; i<strlen(mytext)-2; i++, aptr++, wptr++)
    {
      diff_found |= (*aptr != *wptr);
    }
  ok(!(diff_found), "fgetwc difference found in TEXT mode\n");
  ok(*wptr == '\n', "Carriage return was not skipped\n");
  fclose(tempfh);
  unlink(tempf);
  
  tempfh = fopen(tempf,"wb");
  j = 'a';
  /* pad to almost the length of the internal buffer. Use an odd number of bytes
     to test that we can read wchars that are split across the internal buffer
     boundary */
  for (i=0; i<BUFSIZ-3-strlen(mytext)*sizeof(WCHAR); i++)
    fputc(j,tempfh);
  j = '\r';
  fputwc(j,tempfh);
  j = '\n';
  fputwc(j,tempfh);
  fputws(wtextW,tempfh);
  fputws(wtextW,tempfh);
  fclose(tempfh);
  /* in binary mode, getws/c expects wide characters */
  tempfh = fopen(tempf,"rb"); /* open in BINARY mode */
  j=(BUFSIZ-2)/sizeof(WCHAR)-strlen(mytext);
  fgetws(wtextW,j,tempfh);
  l=ftell(tempfh);
  j=(j-1)*sizeof(WCHAR);
  ok(l==j, "ftell expected %d got %d\n", j, l);
  i=fgetc(tempfh);
  ok(i=='a', "fgetc expected %d got %d\n", 0x61, i);
  l=ftell(tempfh);
  j++;
  ok(l==j, "ftell expected %d got %d\n", j, l);
  fgetws(wtextW,3,tempfh);
  ok(wtextW[0]=='\r',"expected carriage return got %04hx\n", wtextW[0]);
  ok(wtextW[1]=='\n',"expected newline got %04hx\n", wtextW[1]);
  l=ftell(tempfh);
  j += 4;
  ok(l==j, "ftell expected %d got %d\n", j, l);
  for(i=0; i<strlen(mytext); i++)
    wtextW[i] = 0;
  /* the first time we get the string, it should be entirely within the local buffer */
  fgetws(wtextW,LLEN,tempfh);
  l=ftell(tempfh);
  j += (strlen(mytext)-1)*sizeof(WCHAR);
  ok(l==j, "ftell expected %d got %d\n", j, l);
  diff_found = FALSE;
  aptr = mytextW;
  wptr = wtextW;
  for (i=0; i<strlen(mytext)-2; i++, aptr++, wptr++)
    {
      ok(*aptr == *wptr, "Char %d expected %04hx got %04hx\n", i, *aptr, *wptr);
      diff_found |= (*aptr != *wptr);
    }
  ok(!(diff_found), "fgetwc difference found in BINARY mode\n");
  ok(*wptr == '\n', "Should get newline\n");
  for(i=0; i<strlen(mytext); i++)
    wtextW[i] = 0;
  /* the second time we get the string, it should cross the local buffer boundary.
     One of the wchars should be split across the boundary */
  fgetws(wtextW,LLEN,tempfh);
  diff_found = FALSE;
  aptr = mytextW;
  wptr = wtextW;
  for (i=0; i<strlen(mytext)-2; i++, aptr++, wptr++)
    {
      ok(*aptr == *wptr, "Char %d expected %04hx got %04hx\n", i, *aptr, *wptr);
      diff_found |= (*aptr != *wptr);
    }
  ok(!(diff_found), "fgetwc difference found in BINARY mode\n");
  ok(*wptr == '\n', "Should get newline\n");

  free(mytextW);
  fclose(tempfh);
  unlink(tempf);
  free(tempf);
}

static void test_ctrlz( void )
{
  char* tempf;
  FILE *tempfh;
  static const char mytext[]= "This is test_ctrlz";
  char buffer[256];
  int i, j;
  LONG l;

  tempf=_tempnam(".","wne");
  tempfh = fopen(tempf,"wb");
  fputs(mytext,tempfh);
  j = 0x1a; /* a ctrl-z character signals EOF in text mode */
  fputc(j,tempfh);
  j = '\r';
  fputc(j,tempfh);
  j = '\n';
  fputc(j,tempfh);
  j = 'a';
  fputc(j,tempfh);
  fclose(tempfh);
  tempfh = fopen(tempf,"rt"); /* open in TEXT mode */
  ok(fgets(buffer,256,tempfh) != 0,"fgets failed unexpected\n");
  i=strlen(buffer);
  j=strlen(mytext);
  ok(i==j, "returned string length expected %d got %d\n", j, i);
  j+=4; /* ftell should indicate the true end of file */
  l=ftell(tempfh);
  ok(l==j, "ftell expected %d got %d\n", j, l);
  ok(feof(tempfh), "did not get EOF\n");
  fclose(tempfh);
  
  tempfh = fopen(tempf,"rb"); /* open in BINARY mode */
  ok(fgets(buffer,256,tempfh) != 0,"fgets failed unexpected\n");
  i=strlen(buffer);
  j=strlen(mytext)+3; /* should get through newline */
  ok(i==j, "returned string length expected %d got %d\n", j, i);
  l=ftell(tempfh);
  ok(l==j, "ftell expected %d got %d\n", j, l);
  ok(fgets(buffer,256,tempfh) != 0,"fgets failed unexpected\n");
  i=strlen(buffer);
  ok(i==1, "returned string length expected %d got %d\n", 1, i);
  ok(feof(tempfh), "did not get EOF\n");
  fclose(tempfh);
  unlink(tempf);
  free(tempf);
}

static void test_file_put_get( void )
{
  char* tempf;
  FILE *tempfh;
  static const char mytext[]=  "This is a test_file_put_get\n";
  static const char dostext[]= "This is a test_file_put_get\r\n";
  char btext[LLEN];
  WCHAR wtextW[LLEN+1];
  WCHAR *mytextW = NULL, *aptr, *wptr;
  BOOL diff_found = FALSE;
  unsigned int i;

  tempf=_tempnam(".","wne");
  tempfh = fopen(tempf,"wt"); /* open in TEXT mode */
  fputs(mytext,tempfh);
  fclose(tempfh);
  tempfh = fopen(tempf,"rb"); /* open in TEXT mode */
  fgets(btext,LLEN,tempfh);
  ok( strlen(mytext) + 1 == strlen(btext),"TEXT/BINARY mode not handled for write\n");
  ok( btext[strlen(mytext)-1] == '\r', "CR not written\n");
  fclose(tempfh);
  tempfh = fopen(tempf,"wb"); /* open in BINARY mode */
  fputs(dostext,tempfh);
  fclose(tempfh);
  tempfh = fopen(tempf,"rt"); /* open in TEXT mode */
  fgets(btext,LLEN,tempfh);
  ok(strcmp(btext, mytext) == 0,"_O_TEXT read doesn't strip CR\n");
  fclose(tempfh);
  tempfh = fopen(tempf,"rb"); /* open in TEXT mode */
  fgets(btext,LLEN,tempfh);
  ok(strcmp(btext, dostext) == 0,"_O_BINARY read doesn't preserve CR\n");

  fclose(tempfh);
  tempfh = fopen(tempf,"rt"); /* open in TEXT mode */
  fgetws(wtextW,LLEN,tempfh);
  mytextW = AtoW (mytext);
  aptr = mytextW;
  wptr = wtextW;

  for (i=0; i<strlen(mytext); i++, aptr++, wptr++)
    {
      diff_found |= (*aptr != *wptr);
    }
  ok(!(diff_found), "fgetwc doesn't strip CR in TEXT mode\n");
  free(mytextW);
  fclose(tempfh);
  unlink(tempf);
  free(tempf);
}

static void test_file_write_read( void )
{
  char* tempf;
  int tempfd;
  static const char mytext[]=  "This is test_file_write_read\nsecond line\n";
  static const char dostext[]= "This is test_file_write_read\r\nsecond line\r\n";
  char btext[LLEN];
  int ret, i;

  tempf=_tempnam(".","wne");
  tempfd = _open(tempf,_O_CREAT|_O_TRUNC|_O_BINARY|_O_RDWR,
                     _S_IREAD | _S_IWRITE);
  ok( tempfd != -1,
     "Can't open '%s': %d\n", tempf, errno); /* open in BINARY mode */
  ok(_write(tempfd,dostext,strlen(dostext)) == lstrlenA(dostext),
     "_write _O_BINARY bad return value\n");
  _close(tempfd);
  i = lstrlenA(mytext);
  tempfd = _open(tempf,_O_RDONLY|_O_BINARY,0); /* open in BINARY mode */
  ok(_read(tempfd,btext,i) == i,
     "_read _O_BINARY got bad length\n");
  ok( memcmp(dostext,btext,i) == 0,
      "problems with _O_BINARY  _write / _read\n");
  _close(tempfd);
  tempfd = _open(tempf,_O_RDONLY|_O_TEXT); /* open in TEXT mode */
  ok(_read(tempfd,btext,i) == i-1,
     "_read _O_TEXT got bad length\n");
  ok( memcmp(mytext,btext,i-1) == 0,
      "problems with _O_BINARY _write / _O_TEXT _read\n");
  _close(tempfd);
  tempfd = _open(tempf,_O_CREAT|_O_TRUNC|_O_TEXT|_O_RDWR,
                     _S_IREAD | _S_IWRITE);
  ok( tempfd != -1,
     "Can't open '%s': %d\n", tempf, errno); /* open in TEXT mode */
  ok(_write(tempfd,mytext,strlen(mytext)) == lstrlenA(mytext),
     "_write _O_TEXT bad return value\n");
  _close(tempfd);
  tempfd = _open(tempf,_O_RDONLY|_O_BINARY,0); /* open in BINARY mode */
  ok(_read(tempfd,btext,LLEN) == lstrlenA(dostext),
     "_read _O_BINARY got bad length\n");
  ok( memcmp(dostext,btext,strlen(dostext)) == 0,
      "problems with _O_TEXT _write / _O_BINARY _read\n");
  ok( btext[strlen(dostext)-2] == '\r', "CR not written or read\n");
  _close(tempfd);
  tempfd = _open(tempf,_O_RDONLY|_O_TEXT); /* open in TEXT mode */
  ok(_read(tempfd,btext,LLEN) == lstrlenA(mytext),
     "_read _O_TEXT got bad length\n");
  ok( memcmp(mytext,btext,strlen(mytext)) == 0,
      "problems with _O_TEXT _write / _read\n");
  _close(tempfd);

  memset(btext, 0, LLEN);
  tempfd = _open(tempf,_O_APPEND|_O_RDWR); /* open for APPEND in default mode */
  ok(tell(tempfd) == 0, "bad position %u expecting 0\n", tell(tempfd));
  ok(_read(tempfd,btext,LLEN) == lstrlenA(mytext), "_read _O_APPEND got bad length\n");
  ok( memcmp(mytext,btext,strlen(mytext)) == 0, "problems with _O_APPEND _read\n");
  _close(tempfd);

  /* Test reading only \n or \r */
  tempfd = _open(tempf,_O_RDONLY|_O_TEXT); /* open in TEXT mode */
  _lseek(tempfd, -1, FILE_END);
  ret = _read(tempfd,btext,LLEN);
  ok(ret == 1 && *btext == '\n', "_read expected 1 got bad length: %d\n", ret);
  _lseek(tempfd, -2, FILE_END);
  ret = _read(tempfd,btext,LLEN);
  ok(ret == 1 && *btext == '\n', "_read expected '\\n' got bad length: %d\n", ret);
  _lseek(tempfd, -3, FILE_END);
  ret = _read(tempfd,btext,1);
  ok(ret == 1 && *btext == 'e', "_read expected 'e' got \"%.*s\" bad length: %d\n", ret, btext, ret);
  ok(tell(tempfd) == 41, "bad position %u expecting 41\n", tell(tempfd));
  _lseek(tempfd, -3, FILE_END);
  ret = _read(tempfd,btext,2);
  ok(ret == 1 && *btext == 'e', "_read expected 'e' got \"%.*s\" bad length: %d\n", ret, btext, ret);
  ok(tell(tempfd) == 42, "bad position %u expecting 42\n", tell(tempfd));
  _lseek(tempfd, -3, FILE_END);
  ret = _read(tempfd,btext,3);
  ok(ret == 2 && *btext == 'e', "_read expected 'e' got \"%.*s\" bad length: %d\n", ret, btext, ret);
  ok(tell(tempfd) == 43, "bad position %u expecting 43\n", tell(tempfd));
   _close(tempfd);

  ret = unlink(tempf);
  ok( ret == 0 ,"Can't unlink '%s': %d\n", tempf, errno);
  free(tempf);

  tempf=_tempnam(".","wne");
  tempfd = _open(tempf,_O_CREAT|_O_TRUNC|_O_BINARY|_O_RDWR,0);
  ok( tempfd != -1,
     "Can't open '%s': %d\n", tempf, errno); /* open in BINARY mode */
  ok(_write(tempfd,dostext,strlen(dostext)) == lstrlenA(dostext),
     "_write _O_BINARY bad return value\n");
  _close(tempfd);
  tempfd = _open(tempf,_O_RDONLY|_O_BINARY,0); /* open in BINARY mode */
  ok(_read(tempfd,btext,LLEN) == lstrlenA(dostext),
     "_read _O_BINARY got bad length\n");
  ok( memcmp(dostext,btext,strlen(dostext)) == 0,
      "problems with _O_BINARY _write / _read\n");
  ok( btext[strlen(dostext)-2] == '\r', "CR not written or read\n");
  _close(tempfd);
  tempfd = _open(tempf,_O_RDONLY|_O_TEXT); /* open in TEXT mode */
  ok(_read(tempfd,btext,LLEN) == lstrlenA(mytext),
     "_read _O_TEXT got bad length\n");
  ok( memcmp(mytext,btext,strlen(mytext)) == 0,
      "problems with _O_BINARY _write / _O_TEXT _read\n");
  _close(tempfd);

  /* test _read with single bytes. CR should be skipped and LF pulled in */
  tempfd = _open(tempf,_O_RDONLY|_O_TEXT); /* open in TEXT mode */
  for (i=0; i<strlen(mytext); i++)  /* */
    {
      _read(tempfd,btext, 1);
      ok(btext[0] ==  mytext[i],"_read failed at pos %d 0x%02x vs 0x%02x\n", i, btext[0], mytext[i]);
    }
  while (_read(tempfd,btext, 1));
  _close(tempfd);

  /* test _read in buffered mode. Last CR should be skipped but  LF not pulled in */
  tempfd = _open(tempf,_O_RDONLY|_O_TEXT); /* open in TEXT mode */
  i = _read(tempfd,btext, strlen(mytext));
  ok(i == strlen(mytext)-1, "_read_i %d\n", i);
  _close(tempfd);

  ret =_chmod (tempf, _S_IREAD | _S_IWRITE);
  ok( ret == 0,
     "Can't chmod '%s' to read-write: %d\n", tempf, errno);
  ret = unlink(tempf);
  ok( ret == 0 ,"Can't unlink '%s': %d\n", tempf, errno);
  free(tempf);
}

static void test_file_inherit_child(const char* fd_s)
{
    int fd = atoi(fd_s);
    char buffer[32];
    int ret;

    ret =write(fd, "Success", 8);
    ok( ret == 8, "Couldn't write in child process on %d (%s)\n", fd, strerror(errno));
    lseek(fd, 0, SEEK_SET);
    ok(read(fd, buffer, sizeof (buffer)) == 8, "Couldn't read back the data\n");
    ok(memcmp(buffer, "Success", 8) == 0, "Couldn't read back the data\n");
}

static void test_file_inherit_child_no(const char* fd_s)
{
    int fd = atoi(fd_s);
    int ret;

    ret = write(fd, "Success", 8);
    ok( ret == -1 && errno == EBADF, 
       "Wrong write result in child process on %d (%s)\n", fd, strerror(errno));
}

static void create_io_inherit_block( STARTUPINFO *startup, unsigned int count, const HANDLE *handles )
{
    static BYTE block[1024];
    BYTE *wxflag_ptr;
    HANDLE *handle_ptr;
    unsigned int i;

    startup->lpReserved2 = block;
    startup->cbReserved2 = sizeof(unsigned) + (sizeof(char) + sizeof(HANDLE)) * count;
    wxflag_ptr = block + sizeof(unsigned);
    handle_ptr = (HANDLE *)(wxflag_ptr + count);

    *(unsigned*)block = count;
    for (i = 0; i < count; i++)
    {
        wxflag_ptr[i] = 0x81;
        handle_ptr[i] = handles[i];
    }
}

static const char *read_file( HANDLE file )
{
    static char buffer[128];
    DWORD ret;
    SetFilePointer( file, 0, NULL, FILE_BEGIN );
    if (!ReadFile( file, buffer, sizeof(buffer) - 1, &ret, NULL)) ret = 0;
    buffer[ret] = 0;
    return buffer;
}

static void test_stdout_handle( STARTUPINFO *startup, char *cmdline, HANDLE hstdout, BOOL expect_stdout,
                                const char *descr )
{
    const char *data;
    HANDLE hErrorFile;
    SECURITY_ATTRIBUTES sa;
    PROCESS_INFORMATION proc;

    /* make file handle inheritable */
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    hErrorFile = CreateFileA( "fdopen.err", GENERIC_READ|GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, 0, NULL );
    startup->dwFlags    = STARTF_USESTDHANDLES;
    startup->hStdInput  = GetStdHandle( STD_INPUT_HANDLE );
    startup->hStdOutput = hErrorFile;
    startup->hStdError  = GetStdHandle( STD_ERROR_HANDLE );

    CreateProcessA( NULL, cmdline, NULL, NULL, TRUE,
                    CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS, NULL, NULL, startup, &proc );
    winetest_wait_child_process( proc.hProcess );

    data = read_file( hErrorFile );
    if (expect_stdout)
        ok( strcmp( data, "Success" ), "%s: Error file shouldn't contain data\n", descr );
    else
        ok( !strcmp( data, "Success" ), "%s: Wrong error data (%s)\n", descr, data );

    if (hstdout)
    {
        data = read_file( hstdout );
        if (expect_stdout)
            ok( !strcmp( data, "Success" ), "%s: Wrong stdout data (%s)\n", descr, data );
        else
            ok( strcmp( data, "Success" ), "%s: Stdout file shouldn't contain data\n", descr );
    }

    CloseHandle( hErrorFile );
    DeleteFile( "fdopen.err" );
}

static void test_file_inherit( const char* selfname )
{
    int			fd;
    const char*		arg_v[5];
    char 		buffer[16];
    char cmdline[MAX_PATH];
    STARTUPINFO startup;
    SECURITY_ATTRIBUTES sa;
    HANDLE handles[3];

    fd = open ("fdopen.tst", O_CREAT | O_RDWR | O_BINARY, _S_IREAD |_S_IWRITE);
    ok(fd != -1, "Couldn't create test file\n");
    arg_v[0] = selfname;
    arg_v[1] = "tests/file.c";
    arg_v[2] = "inherit";
    arg_v[3] = buffer; sprintf(buffer, "%d", fd);
    arg_v[4] = 0;
    _spawnvp(_P_WAIT, selfname, arg_v);
    ok(tell(fd) == 8, "bad position %u expecting 8\n", tell(fd));
    lseek(fd, 0, SEEK_SET);
    ok(read(fd, buffer, sizeof (buffer)) == 8 && memcmp(buffer, "Success", 8) == 0, "Couldn't read back the data\n");
    close (fd);
    ok(unlink("fdopen.tst") == 0, "Couldn't unlink\n");
    
    fd = open ("fdopen.tst", O_CREAT | O_RDWR | O_BINARY | O_NOINHERIT, _S_IREAD |_S_IWRITE);
    ok(fd != -1, "Couldn't create test file\n");
    arg_v[0] = selfname;
    arg_v[1] = "tests/file.c";
    arg_v[2] = "inherit_no";
    arg_v[3] = buffer; sprintf(buffer, "%d", fd);
    arg_v[4] = 0;
    _spawnvp(_P_WAIT, selfname, arg_v);
    ok(tell(fd) == 0, "bad position %u expecting 0\n", tell(fd));
    ok(read(fd, buffer, sizeof (buffer)) == 0, "Found unexpected data (%s)\n", buffer);
    close (fd);
    ok(unlink("fdopen.tst") == 0, "Couldn't unlink\n");

    /* make file handle inheritable */
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    sprintf(cmdline, "%s file inherit 1", selfname);

    /* init an empty Reserved2, which should not be recognized as inherit-block */
    ZeroMemory(&startup, sizeof(STARTUPINFO));
    startup.cb = sizeof(startup);
    create_io_inherit_block( &startup, 0, NULL );
    test_stdout_handle( &startup, cmdline, 0, FALSE, "empty block" );

    /* test with valid inheritblock */
    handles[0] = GetStdHandle( STD_INPUT_HANDLE );
    handles[1] = CreateFileA( "fdopen.tst", GENERIC_READ|GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, 0, NULL );
    handles[2] = GetStdHandle( STD_ERROR_HANDLE );
    create_io_inherit_block( &startup, 3, handles );
    test_stdout_handle( &startup, cmdline, handles[1], TRUE, "valid block" );
    CloseHandle( handles[1] );
    DeleteFile("fdopen.tst");

    /* test inherit block starting with unsigned zero */
    handles[1] = CreateFileA( "fdopen.tst", GENERIC_READ|GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, 0, NULL );
    create_io_inherit_block( &startup, 3, handles );
    *(unsigned int *)startup.lpReserved2 = 0;
    test_stdout_handle( &startup, cmdline, handles[1], FALSE, "zero count block" );
    CloseHandle( handles[1] );
    DeleteFile("fdopen.tst");

    /* test inherit block with smaller size */
    handles[1] = CreateFileA( "fdopen.tst", GENERIC_READ|GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, 0, NULL );
    create_io_inherit_block( &startup, 3, handles );
    startup.cbReserved2 -= 3;
    test_stdout_handle( &startup, cmdline, handles[1], TRUE, "small size block" );
    CloseHandle( handles[1] );
    DeleteFile("fdopen.tst");

    /* test inherit block with even smaller size */
    handles[1] = CreateFileA( "fdopen.tst", GENERIC_READ|GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, 0, NULL );
    create_io_inherit_block( &startup, 3, handles );
    startup.cbReserved2 = sizeof(unsigned int) + sizeof(HANDLE) + sizeof(char);
    test_stdout_handle( &startup, cmdline, handles[1], FALSE, "smaller size block" );
    CloseHandle( handles[1] );
    DeleteFile("fdopen.tst");

    /* test inherit block with larger size */
    handles[1] = CreateFileA( "fdopen.tst", GENERIC_READ|GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, 0, NULL );
    create_io_inherit_block( &startup, 3, handles );
    startup.cbReserved2 += 7;
    test_stdout_handle( &startup, cmdline, handles[1], TRUE, "large size block" );
    CloseHandle( handles[1] );
    DeleteFile("fdopen.tst");
}

static void test_tmpnam( void )
{
  char name[MAX_PATH] = "abc";
  char *res;

  res = tmpnam(NULL);
  ok(res != NULL, "tmpnam returned NULL\n");
  ok(res[0] == '\\', "first character is not a backslash\n");
  ok(strchr(res+1, '\\') == 0, "file not in the root directory\n");
  ok(res[strlen(res)-1] == '.', "first call - last character is not a dot\n");

  res = tmpnam(name);
  ok(res != NULL, "tmpnam returned NULL\n");
  ok(res == name, "supplied buffer was not used\n");
  ok(res[0] == '\\', "first character is not a backslash\n");
  ok(strchr(res+1, '\\') == 0, "file not in the root directory\n");
  ok(res[strlen(res)-1] != '.', "second call - last character is a dot\n");
}

static void test_chsize( void )
{
    int fd;
    LONG cur, pos, count;
    char temptext[] = "012345678";
    char *tempfile = _tempnam( ".", "tst" );
    
    ok( tempfile != NULL, "Couldn't create test file: %s\n", tempfile );

    fd = _open( tempfile, _O_CREAT|_O_TRUNC|_O_RDWR, _S_IREAD|_S_IWRITE );
    ok( fd > 0, "Couldn't open test file\n" );

    count = _write( fd, temptext, sizeof(temptext) );
    ok( count > 0, "Couldn't write to test file\n" );

    /* get current file pointer */
    cur = _lseek( fd, 0, SEEK_CUR );

    /* make the file smaller */
    ok( _chsize( fd, sizeof(temptext) / 2 ) == 0, "_chsize() failed\n" );

    pos = _lseek( fd, 0, SEEK_CUR );
    ok( cur == pos, "File pointer changed from: %d to: %d\n", cur, pos );
    ok( _filelength( fd ) == sizeof(temptext) / 2, "Wrong file size\n" );

    /* enlarge the file */
    ok( _chsize( fd, sizeof(temptext) * 2 ) == 0, "_chsize() failed\n" ); 

    pos = _lseek( fd, 0, SEEK_CUR );
    ok( cur == pos, "File pointer changed from: %d to: %d\n", cur, pos );
    ok( _filelength( fd ) == sizeof(temptext) * 2, "Wrong file size\n" );

    _close( fd );
    _unlink( tempfile );
    free( tempfile );
}

static void test_fopen_fclose_fcloseall( void )
{
    char fname1[] = "empty1";
    char fname2[] = "empty2";
    char fname3[] = "empty3";
    FILE *stream1, *stream2, *stream3, *stream4;
    int ret, numclosed;

    /* testing fopen() */
    stream1 = fopen(fname1, "w+");
    ok(stream1 != NULL, "The file '%s' was not opened\n", fname1);
    stream2 = fopen(fname2, "w ");
    ok(stream2 != NULL, "The file '%s' was not opened\n", fname2 );
    _unlink(fname3);
    stream3 = fopen(fname3, "r");
    ok(stream3 == NULL, "The file '%s' shouldn't exist before\n", fname3 );
    stream3 = fopen(fname3, "w+");
    ok(stream3 != NULL, "The file '%s' should be opened now\n", fname3 );
    errno = 0xfaceabad;
    stream4 = fopen("", "w+");
    ok(stream4 == NULL && (errno == EINVAL || errno == ENOENT),
       "filename is empty, errno = %d (expected 2 or 22)\n", errno);
    errno = 0xfaceabad;
    stream4 = fopen(NULL, "w+");
    ok(stream4 == NULL && (errno == EINVAL || errno == ENOENT), 
       "filename is NULL, errno = %d (expected 2 or 22)\n", errno);

    /* testing fclose() */
    ret = fclose(stream2);
    ok(ret == 0, "The file '%s' was not closed\n", fname2);
    ret = fclose(stream3);
    ok(ret == 0, "The file '%s' was not closed\n", fname3);
    ret = fclose(stream2);
    ok(ret == EOF, "Closing file '%s' returned %d\n", fname2, ret);
    ret = fclose(stream3);
    ok(ret == EOF, "Closing file '%s' returned %d\n", fname3, ret);

    /* testing fcloseall() */
    numclosed = _fcloseall();
    /* fname1 should be closed here */
    ok(numclosed == 1, "Number of files closed by fcloseall(): %u\n", numclosed);
    numclosed = _fcloseall();
    ok(numclosed == 0, "Number of files closed by fcloseall(): %u\n", numclosed);

    ok(_unlink(fname1) == 0, "Couldn't unlink file named '%s'\n", fname1);
    ok(_unlink(fname2) == 0, "Couldn't unlink file named '%s'\n", fname2);
    ok(_unlink(fname3) == 0, "Couldn't unlink file named '%s'\n", fname3);
}

static void test_fopen_s( void )
{
    const char name[] = "empty1";
    char buff[16];
    FILE *file;
    int ret;
    int len;

    if (!p_fopen_s)
    {
        win_skip("Skipping fopen_s test\n");
        return;
    }
    /* testing fopen_s */
    ret = p_fopen_s(&file, name, "w");
    ok(ret == 0, "fopen_s failed with %d\n", ret);
    ok(file != 0, "fopen_s failed to return value\n");
    fwrite(name, sizeof(name), 1, file);

    ret = fclose(file);
    ok(ret != EOF, "File failed to close\n");

    file = fopen(name, "r");
    ok(file != 0, "fopen failed\n");
    len = fread(buff, 1, sizeof(name), file);
    ok(len == sizeof(name), "File length is %d\n", len);
    buff[sizeof(name)] = '\0';
    ok(strcmp(name, buff) == 0, "File content mismatch! Got %s, expected %s\n", buff, name);

    ret = fclose(file);
    ok(ret != EOF, "File failed to close\n");

    ok(_unlink(name) == 0, "Couldn't unlink file named '%s'\n", name);
}

static void test__wfopen_s( void )
{
    const char name[] = "empty1";
    const WCHAR wname[] = {
       'e','m','p','t','y','1',0
    };
    const WCHAR wmode[] = {
       'w',0
    };
    char buff[16];
    FILE *file;
    int ret;
    int len;

    if (!p__wfopen_s)
    {
        win_skip("Skipping _wfopen_s test\n");
        return;
    }
    /* testing _wfopen_s */
    ret = p__wfopen_s(&file, wname, wmode);
    ok(ret == 0, "_wfopen_s failed with %d\n", ret);
    ok(file != 0, "_wfopen_s failed to return value\n");
    fwrite(name, sizeof(name), 1, file);

    ret = fclose(file);
    ok(ret != EOF, "File failed to close\n");

    file = fopen(name, "r");
    ok(file != 0, "fopen failed\n");
    len = fread(buff, 1, sizeof(name), file);
    ok(len == sizeof(name), "File length is %d\n", len);
    buff[sizeof(name)] = '\0';
    ok(strcmp(name, buff) == 0, "File content mismatch! Got %s, expected %s\n", buff, name);

    ret = fclose(file);
    ok(ret != EOF, "File failed to close\n");

    ok(_unlink(name) == 0, "Couldn't unlink file named '%s'\n", name);
}

static void test_get_osfhandle(void)
{
    int fd;
    char fname[] = "t_get_osfhanle";
    DWORD bytes_written;
    HANDLE handle;

    fd = _sopen(fname, _O_CREAT|_O_RDWR, _SH_DENYRW, _S_IREAD | _S_IWRITE);
    handle = (HANDLE)_get_osfhandle(fd);
    WriteFile(handle, "bar", 3, &bytes_written, NULL);
    _close(fd);
    fd = _open(fname, _O_RDONLY, 0);
    ok(fd != -1, "Coudn't open '%s' after _get_osfhanle()\n", fname);

    _close(fd);
    _unlink(fname);
}

static void test_setmaxstdio(void)
{
    ok(2048 == _setmaxstdio(2048),"_setmaxstdio returned %d instead of 2048\n",_setmaxstdio(2048));
    ok(-1 == _setmaxstdio(2049),"_setmaxstdio returned %d instead of -1\n",_setmaxstdio(2049));
}

static void test_stat(void)
{
    int fd;
    int pipes[2];
    struct stat buf;

    /* Tests for a file */
    fd = open("stat.tst", O_WRONLY | O_CREAT | O_BINARY, _S_IREAD |_S_IWRITE);
    if (fd >= 0)
    {
        ok(fstat(fd, &buf) == 0, "fstat failed: errno=%d\n", errno);
        ok((buf.st_mode & _S_IFMT) == _S_IFREG, "bad format = %06o\n", buf.st_mode);
        ok((buf.st_mode & 0777) == 0666, "bad st_mode = %06o\n", buf.st_mode);
        ok(buf.st_dev == 0, "st_dev is %d, expected 0\n", buf.st_dev);
        ok(buf.st_dev == buf.st_rdev, "st_dev (%d) and st_rdev (%d) differ\n", buf.st_dev, buf.st_rdev);
        ok(buf.st_nlink == 1, "st_nlink is %d, expected 1\n", buf.st_nlink);
        ok(buf.st_size == 0, "st_size is %d, expected 0\n", buf.st_size);

        ok(stat("stat.tst", &buf) == 0, "stat failed: errno=%d\n", errno);
        ok((buf.st_mode & _S_IFMT) == _S_IFREG, "bad format = %06o\n", buf.st_mode);
        ok((buf.st_mode & 0777) == 0666, "bad st_mode = %06o\n", buf.st_mode);
        ok(buf.st_dev == buf.st_rdev, "st_dev (%d) and st_rdev (%d) differ\n", buf.st_dev, buf.st_rdev);
        ok(buf.st_nlink == 1, "st_nlink is %d, expected 1\n", buf.st_nlink);
        ok(buf.st_size == 0, "st_size is %d, expected 0\n", buf.st_size);

        close(fd);
        remove("stat.tst");
    }
    else
        skip("open failed with errno %d\n", errno);

    /* Tests for a char device */
    if (_dup2(0, 10) == 0)
    {
        ok(fstat(10, &buf) == 0, "fstat(stdin) failed: errno=%d\n", errno);
        if ((buf.st_mode & _S_IFMT) == _S_IFCHR)
        {
            ok(buf.st_mode == _S_IFCHR, "bad st_mode=%06o\n", buf.st_mode);
            ok(buf.st_dev == 10, "st_dev is %d, expected 10\n", buf.st_dev);
            ok(buf.st_rdev == 10, "st_rdev is %d, expected 10\n", buf.st_rdev);
            ok(buf.st_nlink == 1, "st_nlink is %d, expected 1\n", buf.st_nlink);
        }
        else
            skip("stdin is not a char device? st_mode=%06o\n", buf.st_mode);
        close(10);
    }
    else
        skip("_dup2 failed with errno %d\n", errno);

    /* Tests for pipes */
    if (_pipe(pipes, 1024, O_BINARY) == 0)
    {
        ok(fstat(pipes[0], &buf) == 0, "fstat(pipe) failed: errno=%d\n", errno);
        ok(buf.st_mode == _S_IFIFO, "bad st_mode=%06o\n", buf.st_mode);
        ok(buf.st_dev == pipes[0], "st_dev is %d, expected %d\n", buf.st_dev, pipes[0]);
        ok(buf.st_rdev == pipes[0], "st_rdev is %d, expected %d\n", buf.st_rdev, pipes[0]);
        ok(buf.st_nlink == 1, "st_nlink is %d, expected 1\n", buf.st_nlink);
        close(pipes[0]);
        close(pipes[1]);
    }
    else
        skip("pipe failed with errno %d\n", errno);
}

static const char* pipe_string="Hello world";

/* How many messages to transfer over the pipe */
#define N_TEST_MESSAGES 3

static void test_pipes_child(int argc, char** args)
{
    int fd;
    int nwritten;
    int i;

    if (argc < 5)
    {
        ok(0, "not enough parameters: %d\n", argc);
        return;
    }

    fd=atoi(args[3]);
    ok(close(fd) == 0, "unable to close %d: %d\n", fd, errno);

    fd=atoi(args[4]);

    for (i=0; i<N_TEST_MESSAGES; i++) {
       nwritten=write(fd, pipe_string, strlen(pipe_string));
       ok(nwritten == strlen(pipe_string), "i %d, expected to write '%s' wrote %d\n", i, pipe_string, nwritten);
       /* let other process wake up so they can show off their "keep reading until EOF" behavior */
       if (i < N_TEST_MESSAGES-1)
           Sleep(100);
    }

    ok(close(fd) == 0, "unable to close %d: %d\n", fd, errno);
}

static void test_pipes(const char* selfname)
{
    int pipes[2];
    char str_fdr[12], str_fdw[12];
    FILE* file;
    const char* arg_v[6];
    char buf[4096];
    char expected[4096];
    int r;
    int i;

    /* Test reading from a pipe with read() */
    if (_pipe(pipes, 1024, O_BINARY) < 0)
    {
        ok(0, "pipe failed with errno %d\n", errno);
        return;
    }

    arg_v[0] = selfname;
    arg_v[1] = "tests/file.c";
    arg_v[2] = "pipes";
    arg_v[3] = str_fdr; sprintf(str_fdr, "%d", pipes[0]);
    arg_v[4] = str_fdw; sprintf(str_fdw, "%d", pipes[1]);
    arg_v[5] = NULL;
    proc_handles[0] = (HANDLE)_spawnvp(_P_NOWAIT, selfname, arg_v);
    ok(close(pipes[1]) == 0, "unable to close %d: %d\n", pipes[1], errno);

    for (i=0; i<N_TEST_MESSAGES; i++) {
       r=read(pipes[0], buf, sizeof(buf)-1);
       ok(r == strlen(pipe_string), "i %d, got %d\n", i, r);
       if (r > 0)
           buf[r]='\0';
       ok(strcmp(buf, pipe_string) == 0, "expected to read '%s', got '%s'\n", pipe_string, buf);
   }

    r=read(pipes[0], buf, sizeof(buf)-1);
    ok(r == 0, "expected to read 0 bytes, got %d\n", r);
    ok(close(pipes[0]) == 0, "unable to close %d: %d\n", pipes[0], errno);

    /* Test reading from a pipe with fread() */
    if (_pipe(pipes, 1024, O_BINARY) < 0)
    {
        ok(0, "pipe failed with errno %d\n", errno);
        return;
    }

    arg_v[0] = selfname;
    arg_v[1] = "tests/file.c";
    arg_v[2] = "pipes";
    arg_v[3] = str_fdr; sprintf(str_fdr, "%d", pipes[0]);
    arg_v[4] = str_fdw; sprintf(str_fdw, "%d", pipes[1]);
    arg_v[5] = NULL;
    proc_handles[1] = (HANDLE)_spawnvp(_P_NOWAIT, selfname, arg_v);
    ok(close(pipes[1]) == 0, "unable to close %d: %d\n", pipes[1], errno);
    file=fdopen(pipes[0], "r");

    /* In blocking mode, fread will keep calling read() until it gets
     * enough bytes, or EOF, even on Unix.  (If this were a Unix terminal
     * in cooked mode instead of a pipe, it would also stop on EOL.)
     */
    expected[0] = 0;
    for (i=0; i<N_TEST_MESSAGES; i++)
       strcat(expected, pipe_string);
    r=fread(buf, 1, sizeof(buf)-1, file);
    ok(r == strlen(expected), "fread() returned %d: ferror=%d\n", r, ferror(file));
    if (r > 0)
       buf[r]='\0';
    ok(strcmp(buf, expected) == 0, "got '%s' expected '%s'\n", buf, expected);

    /* Let child close the file before we read, so we can sense EOF reliably */
    Sleep(100);
    r=fread(buf, 1, sizeof(buf)-1, file);
    ok(r == 0, "fread() returned %d instead of 0\n", r);
    ok(ferror(file) == 0, "got ferror() = %d\n", ferror(file));
    ok(feof(file), "feof() is false!\n");

    ok(fclose(file) == 0, "unable to close the pipe: %d\n", errno);
}

static void test_unlink(void)
{
    FILE* file;
    ok(mkdir("test_unlink") == 0, "unable to create test dir\n");
    file = fopen("test_unlink\\empty", "w");
    ok(file != NULL, "unable to create test file\n");
    if(file)
      fclose(file);
    ok(_unlink("test_unlink") != 0, "unlinking a non-empty directory must fail\n");
    unlink("test_unlink\\empty");
    rmdir("test_unlink");
}

static void test_dup2(void)
{
    ok(-1 == _dup2(0, -1), "expected _dup2 to fail when second arg is negative\n" );
}

START_TEST(file)
{
    int arg_c;
    char** arg_v;

    init();

    arg_c = winetest_get_mainargs( &arg_v );

    /* testing low-level I/O */
    if (arg_c >= 3)
    {
        if (strcmp(arg_v[2], "inherit") == 0)
            test_file_inherit_child(arg_v[3]);
        else if (strcmp(arg_v[2], "inherit_no") == 0)
            test_file_inherit_child_no(arg_v[3]);
        else if (strcmp(arg_v[2], "pipes") == 0)
            test_pipes_child(arg_c, arg_v);
        else
            ok(0, "invalid argument '%s'\n", arg_v[2]);
        return;
    }
    test_dup2();
    test_file_inherit(arg_v[0]);
    test_file_write_read();
    test_chsize();
    test_stat();
    test_unlink();

    /* testing stream I/O */
    test_filbuf();
    test_fdopen();
    test_fopen_fclose_fcloseall();
    test_fopen_s();
    test__wfopen_s();
    test_fileops();
    test_asciimode();
    test_asciimode2();
    test_readmode(FALSE); /* binary mode */
    test_readmode(TRUE);  /* ascii mode */
    test_readboundary();
    test_fgetc();
    test_fputc();
    test_flsbuf();
    test_fgetwc();
    test_ctrlz();
    test_file_put_get();
    test_tmpnam();
    test_get_osfhandle();
    test_setmaxstdio();
    test_pipes(arg_v[0]);

    /* Wait for the (_P_NOWAIT) spawned processes to finish to make sure the report
     * file contains lines in the correct order
     */
    WaitForMultipleObjects(sizeof(proc_handles)/sizeof(proc_handles[0]), proc_handles, TRUE, 5000);
}
