/**
 * fez-util.c
 *
 * Misc utility functions.
 * 
 * (c) 2008, Curtis Nottberg
 *
 * Authors:
 *   Curtis Nottberg
 */

// These functions should probably be replaced with glib equivalents.
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xmlreader.h>

#include <glib.h>

#include "config.h"

// Some of the targeted platforms don't support the g_strcmp0 function.
// If needed, replace the functionality here.
#if (HAS_G_STRCMP0 == 0)
int g_strcmp0(const char *str1, const char *str2)
{
	return strcmp(str1, str2);	
}
#endif

// Work Around an annoying bug in libxml2 where the MINGW version of the library
// doesn't correctly init the xmlFree pointer.  Just call g_free() directly in the
// case of windows. 
void MyXmlFree(xmlChar *MemPtr)
{
#if WIN32
    g_free(MemPtr);
#else
    xmlFree(MemPtr);
#endif
}


