#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <string.h>

#include "buffers.h"
#include "avl.h"
#include "taxinfo.h"

const char UsageString[] = "Usage: taxonomy-reader\n\t<GenBank taxid to name map file (e.g. taxdump/names from ftp://ftp.ncbi.nih.gov/pub/taxonomy/taxdump.tar.gz)>\n\t<GenBank taxonomic hierarcy file (e.g. taxdump/nodes from ftp://ftp.ncbi.nih.gov/pub/taxonomy/taxdump.tar.gz)>\n\t[ integer index (0-based) of the field containing tax ID in each tab-separated input line; the default is field 1]\n";

/*#define DEBUG_ME*/

#define MAX_LINE_BUFFER 2048L

/*---------------------------------------------------------------------------------------------------- */

struct      avl_table *	nameTagAVL  = NULL;

/*---------------------------------------------------------------------------------------------------- */

struct		bufferedString *globalNameBuffer;

/*---------------------------------------------------------------------------------------------------- */

struct		storedNameTag
{
  long		taxID,
  startIndex,
  length,
  taxonomic_level;
  
  struct		storedNameTag * parent;
};

/*---------------------------------------------------------------------------------------------------- */

struct storedNameTag * allocateNameTag (void)
{
  struct storedNameTag *newS = (struct storedNameTag*)malloc (sizeof (struct storedNameTag));
  check_pointer		(newS);
  newS->taxID			= 0;
  newS->startIndex	= 0;
  newS->length		= 0;
  newS->parent		= NULL;
  newS->taxonomic_level	= -1;
  return newS;
}

/*---------------------------------------------------------------------------------------------------- */

int compare_tags (const void *avl_a, const void *avl_b, void * xtra)
{
  long n1 = ((struct storedNameTag*)avl_a)->taxID;
  long n2 = ((struct storedNameTag*)avl_b)->taxID;
  
  if (n1 > n2) return 1;
  if (n1 < n2) return -1;
  return 0;
}

/*---------------------------------------------------------------------------------------------------- */

struct bufferedString * nameByID (long taxID)
{
  static struct			storedNameTag queryTag;
  struct storedNameTag *	res;
  struct bufferedString * resStr = NULL;
		
  queryTag.taxID = taxID;
  res = (struct storedNameTag *)avl_find(nameTagAVL, &queryTag);
  if (res)
  {
    resStr = allocateNewString();
    appendRangeToString(resStr,globalNameBuffer,res->startIndex,res->startIndex+res->length-1);
  }
  return resStr;
}

/*---------------------------------------------------------------------------------------------------- */

struct storedNameTag * tagByID (long taxID)
{
  static struct	storedNameTag queryTag;
  queryTag.taxID = taxID;
  return  (struct storedNameTag *)avl_find(nameTagAVL, &queryTag);
}

/*---------------------------------------------------------------------------------------------------- */

struct bufferedString * walkPath (long taxID)
{
  struct storedNameTag  * currentTag		= tagByID(taxID);
  if (!currentTag)
    return NULL;
  struct bufferedString * resStr			= allocateNewString();
  long   k, last_level = NUMBER_OF_TAX_FIELDS;
  while (currentTag) {
    if (currentTag->taxonomic_level >= 0) {
      for (k = last_level-1; k > currentTag->taxonomic_level; k--) {
        appendCharacterToString (resStr,'\t');
        appendCharacterToString (resStr,'n');
      }
      last_level = currentTag->taxonomic_level;
    }
    else {
      currentTag = currentTag->parent;
      continue;
    }
    
    if (last_level < NUMBER_OF_TAX_FIELDS)
      appendCharacterToString(resStr,'\t');
    
    
    for (k=currentTag->startIndex+currentTag->length-1; k>=currentTag->startIndex; k--)
      appendCharacterToString(resStr,globalNameBuffer->sData[k]);
    
    currentTag = currentTag->parent;
  }
  
  
  return resStr;
}

/*---------------------------------------------------------------------------------------------------- */


/*---------------------------------------------------------------------------------------------------- */

int main (int argc, const char * argv[])
{
  FILE							*inFile,
  *structFile,
  *speciesIDDumpFile;
  
  struct	bufferedString			*scientificName = allocateNewString(),
  *no_rank		= allocateNewString(),
  **currentBuffers,
  *qry;
  
  struct  storedNameTag			*aTag,
  *aTag2;
  
  int	automatonState			= 0,
  currentField            = 0,
  tax_id_field            = 1;
  
  char  currentChar				= 0,
        line_buffer       [MAX_LINE_BUFFER];
  
  long	currentLineID			= 1,
  expectedFields          = 4,
  tax_level               = 0,
  current_tab,
  last_tab,
  tab_count,
  indexer;
  
  size_t  current_buffer_index    = 0L,
          current_buffer_size     = 0L;
  
  
  
  appendCharBufferToString(scientificName,	"scientific name");
  appendCharBufferToString(no_rank,			"no rank");
  
  globalNameBuffer = allocateNewString();
  currentBuffers   = (struct	bufferedString**)malloc (expectedFields*sizeof (struct	bufferedString*));
  nameTagAVL		 = avl_create (compare_tags, NULL, NULL);
  
  for (indexer = 0; indexer < expectedFields; indexer++)
    currentBuffers[indexer] = allocateNewString();
  
		
  if (argc != 3 && argc != 4)
  {
    fprintf (stderr,"%s", UsageString);
    return 1;
  }
		
  inFile		= fopen (argv[1], "rb");
  
  if (!inFile)
  {
	   fprintf (stderr,"Failed to open input file: %s\n", argv[1]);
	   return 1;
  }
  
  structFile		= fopen (argv[2], "rb");
  
  if (!structFile)
  {
	   fprintf (stderr,"Failed to open input file: %s\n", argv[2]);
	   return 1;
  }
  
  if (argc == 4) {
    tax_id_field = atol (argv[3]);
    if (tax_id_field < 0) {
      fprintf (stderr,"Expected a non-negative integer field index: %s\n", argv[3]);
      return 1;
    }
  }
  
  
  for (;;)
  {
    if (current_buffer_index >= current_buffer_size) {
      current_buffer_size = fread (line_buffer, 1L, MAX_LINE_BUFFER, inFile);
      if (current_buffer_size == 0L) {
        break;
      }
      current_buffer_index = 1L;
      currentChar = line_buffer[0];
    } else {
      currentChar = line_buffer[current_buffer_index++];
    }
    
    switch (automatonState)
    {
      case 0: /* start of the line; expecting numbers */
        if (currentChar >= '0' && currentChar <='9')
        {
          automatonState = 1; /* reading sequence ID */
          appendCharacterToString(currentBuffers[currentField],currentChar);
        }
        else
          if (!(currentChar == '\n' || currentChar == '\r'))
            reportErrorLine ("Could not find a valid sequence ID to start the line",currentLineID);
        break;
        
      case 1: /* reading sequence ID */
        if (currentChar >= '0' && currentChar <='9')
          appendCharacterToString(currentBuffers[currentField],currentChar);
        else
          if (currentChar == '\t')
            automatonState = 2;
          else
            reportErrorLine ("Expected a tab following the tax ID",currentLineID);
        break;
        
      case 2: /* looking for a | */
        if (currentChar != '|')
          reportErrorLine ("Expected a '|' following the tab",currentLineID);
        else
          automatonState = 3;
        break;
        
      case 3: /* looking for a \t or a \n|\r*/
        if (currentChar == '\t')
        {
          automatonState = 4;
          currentField   ++;
          if (currentField == expectedFields)
            reportErrorLine ("Too many fields in file 1",currentLineID);
        }
        else
          if (currentChar == '\n' || currentChar == '\r')
          {
            if (currentField < expectedFields-1)
              reportErrorLine ("Too few fields",currentLineID);
            automatonState = 0;
            currentLineID ++;
            currentField = 0;
            if (compare_strings(currentBuffers[3],scientificName)==0)
            {
              aTag = allocateNameTag();
              aTag->taxID = atoi(currentBuffers[0]->sData);
              aTag->startIndex = globalNameBuffer->sLength;
              aTag->length	 = appendRangeToString(globalNameBuffer,currentBuffers[1],0,currentBuffers[1]->sLength-1);
              if (aTag->length <= 0)
                reportErrorLine ("Empty name tag",currentLineID);
              if (*avl_probe(nameTagAVL,aTag) != aTag)
                reportErrorLine ("Duplicate name tag",currentLineID);
              
            }
            for (indexer = 0; indexer < expectedFields; indexer++)
              clear_buffered_string(currentBuffers[indexer]);
          }
          else
            reportErrorLine ("Expected a tab following the '|'",currentLineID);
        break;
        
      case 4: /* read a field */
        if (currentChar == '\t')
          automatonState = 2;
        else
          if (currentChar == '\n' || currentChar == '\r')
            reportErrorLine ("Unexpected end-of-line",currentLineID);
          else
            appendCharacterToString(currentBuffers[currentField],currentChar);
        break;
        
    }
  }
  
  fclose (inFile);
		
  for (indexer = 0L; indexer < expectedFields; indexer++)
    destroy_string(currentBuffers[indexer]);
  
  free(currentBuffers);
  expectedFields = 13;
  currentBuffers   = (struct	bufferedString**)malloc (expectedFields*sizeof (struct	bufferedString*));
  
  for (indexer = 0; indexer < expectedFields; indexer++)
    currentBuffers[indexer] = allocateNewString();
		
  automatonState = 0;
  currentLineID  = 1;
  currentField   = 0;
  current_buffer_index = 0L;
  current_buffer_size = 0L;
  
  for (;;) {
    if (current_buffer_index >= current_buffer_size) {
      current_buffer_size = fread (line_buffer, 1L, MAX_LINE_BUFFER, structFile);
      if (current_buffer_size == 0L) {
        break;
      }
      current_buffer_index = 1L;
      currentChar = line_buffer[0];
    } else {
      currentChar = line_buffer[current_buffer_index++];
    }
    switch (automatonState)
    {
      case 0: /* start of the line; expecting numbers */
        if (currentChar >= '0' && currentChar <='9')
        {
          automatonState = 1; /* reading sequence ID */
          appendCharacterToString(currentBuffers[currentField],currentChar);
        }
        else
          if (!(currentChar == '\n' || currentChar == '\r'))
            reportErrorLine ("Could not find a valid sequence ID to start the line",currentLineID);
        break;
        
      case 1: /* reading sequence ID */
        if (currentChar >= '0' && currentChar <='9')
          appendCharacterToString(currentBuffers[currentField],currentChar);
        else
          if (currentChar == '\t')
            automatonState = 2;
          else
            reportErrorLine ("Expected a tab following the tax ID",currentLineID);
        break;
        
      case 2: /* looking for a | */
        if (currentChar != '|')
          reportErrorLine ("Expected a '|' following the tab",currentLineID);
        else
          automatonState = 3;
        break;
        
      case 3: /* looking for a \t or a \n|\r*/
        if (currentChar == '\t')
        {
          automatonState = 4;
          currentField   ++;
          if (currentField == expectedFields)
            reportErrorLine ("Too many fields",currentLineID);
        }
        else
          if (currentChar == '\n' || currentChar == '\r')
          {
            if (currentField < expectedFields-1)
              reportErrorLine ("Too few fields",currentLineID);
            
            aTag  = tagByID(atoi(currentBuffers[0]->sData));
            aTag2 = tagByID(atoi(currentBuffers[1]->sData));
            
            if (! (aTag && aTag2))
              reportErrorLine ("Invalid ID tag",currentLineID);
            if (aTag2 != aTag)
            {
              aTag->parent     = aTag2;
              aTag->taxonomic_level = -1;
              
              for (tax_level = 0; tax_level < NUMBER_OF_TAX_FIELDS; tax_level ++)
                if (compare_string_and_char(currentBuffers[2],rankLabels[tax_level])==0) {
                  aTag->taxonomic_level = tax_level;
                  break;
                }
            } else { // root
              aTag->taxonomic_level = 0;
            }
            
            currentField = 0;
            automatonState = 0;
            currentLineID ++;
            
            for (indexer = 0; indexer < expectedFields; indexer++)
              clear_buffered_string(currentBuffers[indexer]);
          }
          else
            reportErrorLine ("Expected a tab following the '|'",currentLineID);
        break;
        
      case 4: /* read a field */
        if (currentChar == '\t')
          automatonState = 2;
        else
          if (currentChar == '\n' || currentChar == '\r')
            reportErrorLine ("Unexpected end-of-line",currentLineID);
          else
            appendCharacterToString(currentBuffers[currentField],currentChar);
        break;
        
    }
  }
  
  fclose (structFile);

  while (!feof (stdin)) {
    expectedFields = -1L;
    
    if (fgets     (line_buffer, MAX_LINE_BUFFER, stdin)) {
      last_tab    = 0;
      current_tab = 0;
      tab_count   = 0;
      
      for (;;) {
        if (line_buffer[current_tab] == '\t' || line_buffer[current_tab] == '\n' || line_buffer[current_tab] == '\0') {
            if (tab_count == tax_id_field) {
              if (current_tab > last_tab) {
                currentChar = line_buffer[current_tab];
                line_buffer[current_tab] = '\0';
                expectedFields = atoi (line_buffer + last_tab);
                line_buffer[current_tab] = currentChar;
              }
              break;
            }
            if (line_buffer[current_tab] == '\0') {
              break;
            }
            last_tab = current_tab + 1;
            tab_count ++;
        }
        current_tab ++;
      }
      
      if (expectedFields > 0L) {
        qry = walkPath	(expectedFields);
        if (!qry) continue;
        current_tab = 0;
        while (line_buffer[current_tab] != '\n' && line_buffer[current_tab]) {
          printf ("%c", line_buffer[current_tab++]);
        }
        printf ("\t");
        for (indexer = qry->sLength-1; indexer >= 0; indexer--) {
          printf("%c", qry->sData[indexer]);
        }
        printf("\n");
        destroy_string(qry);
      }
      
    }
  }
  
  return 0;
}
