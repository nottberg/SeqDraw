#include <glib.h>

#include "config.h"
#include "sqd-layout.h"

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xmlreader.h>

// Eliminate all of the preceding, trailing, and extraneous whitespace in a string.
static void normalize_content_str(gchar *str)
{
    gchar last;
    gint  cidx;
    gint  inspt;

    // Chop all of the preceding and trailing whitespace.
    g_strstrip(str);

    // Replace all the newlines and such with spaces
    g_strdelimit(str, "\n\r\t", ' ');

    // Check if there are charaters to process.
    if(str[0] == '\0')
        return;

    // Scan the string collapsing all whitespace down to a single space between words.    
    last  = str[0];
    cidx  = 1;
    inspt = 1;

    while( str[cidx] != '\0' )
    {
        // Check which copying state we are in.
        if( str[cidx] != ' ' )
        {
            // Copying valid characters
            str[inspt] = str[cidx];
            inspt += 1;
        }
        else if( last == ' ' )
        {
            // Waiting to exit a string of spaces
        }
        else
        {
            // Copy over the first space of a potential string of spaces.
            str[inspt] = str[cidx];
            inspt += 1;
        }

        // New last character
        last = str[cidx]; 

        // Next character
        cidx += 1;
    }

    // Copy over the null
    str[inspt] = str[cidx];
}

parse_name_and_description( SQDLayout *SL, xmlDocPtr DocPtr, xmlNodePtr SeqNode )
{
    xmlXPathContextPtr XPath;       
    xmlChar           *tmpStr;  
	xmlNodePtr         FNode;
	xmlXPathObjectPtr  xpathlist;

	// Build a context which XPath can reference from
	XPath = xmlXPathNewContext( DocPtr );
    XPath->node = SeqNode;
    
    // Add the namespaces of interest.
    xmlXPathRegisterNs(XPath, "sqd", "http://nottbergbros.com/seqdraw");

    // Sequence diagram name.
	xpathlist = xmlXPathEvalExpression("sqd:name", XPath);

    g_print( "query: 0x%x, %d\n", xpathlist, xmlXPathNodeSetIsEmpty(xpathlist->nodesetval) );

    // If nodes where found then parse them.
	if( xpathlist && !xmlXPathNodeSetIsEmpty(xpathlist->nodesetval) )
	{
        // Get a pointer to the current node.
	    FNode = xpathlist->nodesetval->nodeTab[0];
		
        tmpStr = xmlNodeGetContent(FNode);
	    normalize_content_str(tmpStr);

        sqd_layout_set_name(SL, tmpStr);

        if(tmpStr) xmlFree(tmpStr);
          
        xmlFree(xpathlist);
    }

    // Sequence diagram description.
	xpathlist = xmlXPathEvalExpression("sqd:description", XPath);

    g_print( "query: 0x%x, %d\n", xpathlist, xmlXPathNodeSetIsEmpty(xpathlist->nodesetval) );

    // If nodes where found then parse them.
	if( xpathlist && !xmlXPathNodeSetIsEmpty(xpathlist->nodesetval) )
	{
        // Get a pointer to the current node.
	    FNode = xpathlist->nodesetval->nodeTab[0];
		
        tmpStr = xmlNodeGetContent(FNode);
	    normalize_content_str(tmpStr);

        sqd_layout_set_description(SL, tmpStr);

        if(tmpStr) xmlFree(tmpStr);
          
        xmlFree(xpathlist);
    }

	// Free up the libxml structures.
	xmlXPathFreeContext( XPath );    
}

parse_actor_list( SQDLayout *SL, xmlDocPtr DocPtr, xmlNodePtr SeqNode )
{
    xmlXPathContextPtr XPath;       
    xmlChar           *idStr;
    xmlNodeSetPtr      nodeset;  
	xmlNodePtr         FNode;
	xmlXPathObjectPtr  xpathlist;
    guint32            NodeIndx;

	// Build a context which XPath can reference from
	XPath = xmlXPathNewContext( DocPtr );
    XPath->node = SeqNode;
    
    // Add the namespaces of interest.
    xmlXPathRegisterNs(XPath, "sqd", "http://nottbergbros.com/seqdraw");

    // Sequence diagram description.
    // Load Planned test cases from the expected file.
    // Query for decoder definitions.
	xpathlist = xmlXPathEvalExpression("sqd:actor-list/sqd:actor", XPath);

    g_print( "query: 0x%x, %d\n", xpathlist, xmlXPathNodeSetIsEmpty(xpathlist->nodesetval) );

    // If nodes where found then parse them.
	if( xpathlist && !xmlXPathNodeSetIsEmpty(xpathlist->nodesetval) )
	{
   		nodeset = xpathlist->nodesetval; 
	    g_print( "actor node count: %d\n", nodeset->nodeNr );    

        // Cycle through the decoder specs
		for (NodeIndx = 0; NodeIndx < nodeset->nodeNr; NodeIndx++)    
		{
            xmlChar           *idStr;
            xmlChar           *nameStr;

		    // Get a pointer to the current node.
		    FNode = nodeset->nodeTab[NodeIndx];

            idStr = xmlGetProp(FNode,"id");
            if(idStr == NULL)
            {
                g_error("Actor descriptions require a id property.\n");
                return TRUE;
            }

            nameStr = xmlGetProp(FNode,"name");
		
            sqd_layout_add_actor(SL, idStr, NodeIndx, nameStr);

            if(idStr) xmlFree(idStr);
            if(nameStr) xmlFree(nameStr);
		}

        xmlFree(xpathlist);
    }

	// Free up the libxml structures.
	xmlXPathFreeContext( XPath );    
}

parse_event_slot_list( SQDLayout *SL, xmlDocPtr DocPtr, xmlNodePtr SeqNode, guint slotIndex )
{
    xmlXPathContextPtr XPath;       
    xmlChar           *idStr;
    xmlNodeSetPtr      nodeset;  
	xmlNodePtr         FNode;
	xmlXPathObjectPtr  xpathlist;
    guint32            NodeIndx;

	// Build a context which XPath can reference from
	XPath = xmlXPathNewContext( DocPtr );
    XPath->node = SeqNode;
    
    // Add the namespaces of interest.
    xmlXPathRegisterNs(XPath, "sqd", "http://nottbergbros.com/seqdraw");

    // Sequence diagram description.
    // Load Planned test cases from the expected file.
    // Query for decoder definitions.
	xpathlist = xmlXPathEvalExpression("sqd:*[contains(name(),'event')]", XPath);

    g_print( "query: 0x%x, %d\n", xpathlist, xmlXPathNodeSetIsEmpty(xpathlist->nodesetval) );

    // If nodes where found then parse them.
	if( xpathlist && !xmlXPathNodeSetIsEmpty(xpathlist->nodesetval) )
	{
   		nodeset = xpathlist->nodesetval; 
	    g_print( "event node count: %d\n", nodeset->nodeNr );    

        // Cycle through the decoder specs
		for (NodeIndx = 0; NodeIndx < nodeset->nodeNr; NodeIndx++)    
		{
            xmlChar           *idStr       = NULL;
            xmlChar           *startActor  = NULL;
            xmlChar           *endActor    = NULL;
            xmlChar           *topLabel    = NULL;
            xmlChar           *bottomLabel = NULL;

		    // Get a pointer to the current node.
		    FNode = nodeset->nodeTab[NodeIndx];

            // All event nodes require an id string; check for that here.
            idStr = xmlGetProp(FNode,"id");
            if(idStr == NULL)
            {
                g_error("Event descriptions require a id property.\n");
                return TRUE;
            }

            // Determine the type of event being dealt with
            if( g_strcmp0(FNode->name, "event") == 0 )
            {
                // Regular event between two actors.
                startActor = xmlGetProp(FNode,"start-actor");
                if(startActor == NULL)
                {
                    g_error("Event descriptions require a start-actor property.\n");
                    return TRUE;
                }

                g_print("SA: %s\n", startActor);

                endActor = xmlGetProp(FNode,"end-actor");
                if(endActor == NULL)
                {
                    g_error("Event descriptions require a end-actor property.\n");
                    return TRUE;
                }

                topLabel = xmlGetProp(FNode,"top-label");
                bottomLabel = xmlGetProp(FNode,"bottom-label");
		
                sqd_layout_add_event(SL, idStr, slotIndex, startActor, endActor, topLabel, bottomLabel);

            }
            else if( g_strcmp0(FNode->name, "step-event") == 0 )
            {
                // Process event, representing work by a single actor.
                startActor = xmlGetProp(FNode,"actor");
                if(startActor == NULL)
                {
                    g_error("Step event descriptions require an actor property.\n");
                    return TRUE;
                }

                topLabel = xmlGetProp(FNode,"label");

                sqd_layout_add_step_event(SL, idStr, slotIndex, startActor, topLabel);
            }
            else if( g_strcmp0(FNode->name, "ext-to-event") == 0 )
            {
                // Process event, representing work by a single actor.
                startActor = xmlGetProp(FNode,"actor");
                if(startActor == NULL)
                {
                    g_error("External event descriptions require an actor property.\n");
                    return TRUE;
                }

                topLabel = xmlGetProp(FNode,"label");

                sqd_layout_add_external_event(SL, idStr, slotIndex, startActor, topLabel, FALSE);
            }
            else if( g_strcmp0(FNode->name, "ext-from-event") == 0 )
            {
                // Process event, representing work by a single actor.
                startActor = xmlGetProp(FNode,"actor");
                if(startActor == NULL)
                {
                    g_error("External event descriptions require an actor property.\n");
                    return TRUE;
                }

                topLabel = xmlGetProp(FNode,"label");

                sqd_layout_add_external_event(SL, idStr, slotIndex, startActor, topLabel, TRUE);
            }

            // Check for storage that needs to be freed.
            if(idStr)       xmlFree(idStr);
            if(startActor)  xmlFree(startActor);
            if(endActor)    xmlFree(endActor);
            if(topLabel)    xmlFree(topLabel);
            if(bottomLabel) xmlFree(bottomLabel);
		}

        xmlFree(xpathlist);
    }

	// Free up the libxml structures.
	xmlXPathFreeContext( XPath );    
}

parse_event_list( SQDLayout *SL, xmlDocPtr DocPtr, xmlNodePtr SeqNode )
{
    xmlXPathContextPtr XPath;       
    xmlChar           *idStr;
    xmlNodeSetPtr      nodeset;  
	xmlNodePtr         FNode;
	xmlXPathObjectPtr  xpathlist;
    guint32            NodeIndx;

	// Build a context which XPath can reference from
	XPath = xmlXPathNewContext( DocPtr );
    XPath->node = SeqNode;
    
    // Add the namespaces of interest.
    xmlXPathRegisterNs(XPath, "sqd", "http://nottbergbros.com/seqdraw");

    // Sequence diagram description.
    // Load Planned test cases from the expected file.
    // Query for decoder definitions.
	xpathlist = xmlXPathEvalExpression("sqd:event-list/sqd:slot", XPath);

    g_print( "query: 0x%x, %d\n", xpathlist, xmlXPathNodeSetIsEmpty(xpathlist->nodesetval) );

    // If nodes where found then parse them.
	if( xpathlist && !xmlXPathNodeSetIsEmpty(xpathlist->nodesetval) )
	{
   		nodeset = xpathlist->nodesetval; 
	    g_print( "slot node count: %d\n", nodeset->nodeNr );    

        // Cycle through the slot definitions
		for (NodeIndx = 0; NodeIndx < nodeset->nodeNr; NodeIndx++)    
		{
		    // Get a pointer to the current node.
		    FNode = nodeset->nodeTab[NodeIndx];

            // Parse the events in this slot
            parse_event_slot_list( SL, DocPtr, FNode, NodeIndx );
		}

        xmlFree(xpathlist);
    }

	// Free up the libxml structures.
	xmlXPathFreeContext( XPath );    
}

parse_note_list( SQDLayout *SL, xmlDocPtr DocPtr, xmlNodePtr SeqNode )
{
    xmlXPathContextPtr XPath;       
    xmlChar           *idStr;
    xmlNodeSetPtr      nodeset;  
	xmlNodePtr         FNode;
	xmlXPathObjectPtr  xpathlist;
    guint32            NodeIndx;

	// Build a context which XPath can reference from
	XPath = xmlXPathNewContext( DocPtr );
    XPath->node = SeqNode;
    
    // Add the namespaces of interest.
    xmlXPathRegisterNs(XPath, "sqd", "http://nottbergbros.com/seqdraw");

    // Sequence diagram description.
    // Load Planned test cases from the expected file.
    // Query for decoder definitions.
	xpathlist = xmlXPathEvalExpression("sqd:note-list/sqd:note", XPath);

    g_print( "query: 0x%x, %d\n", xpathlist, xmlXPathNodeSetIsEmpty(xpathlist->nodesetval) );

    // If nodes where found then parse them.
	if( xpathlist && !xmlXPathNodeSetIsEmpty(xpathlist->nodesetval) )
	{
   		nodeset = xpathlist->nodesetval; 
	    g_print( "note node count: %d\n", nodeset->nodeNr );    

        // Cycle through the decoder specs
		for (NodeIndx = 0; NodeIndx < nodeset->nodeNr; NodeIndx++)    
		{
//sqd_layout_add_note(SL, "note1", 0, NOTE_REFTYPE_EVENT_START, "e2", "Test Note 1");
//<sqd:note id="note1" reference="event-start" refid="e6">
//     Test Note 1
//</sqd:note>

//            <sqd:event id="e1" slot="0" start-actor="host" end-actor="firmware" top-label="Bob"/>

            xmlChar           *idStr;
            xmlChar           *RefType;
            xmlChar           *RefId;
            xmlChar           *NoteStr;
            guint              RefTypeValue;

		    // Get a pointer to the current node.
		    FNode = nodeset->nodeTab[NodeIndx];

            idStr = xmlGetProp(FNode,"id");
            if(idStr == NULL)
            {
                g_error("Note descriptions require a id property.\n");
                return TRUE;
            }


            RefType = xmlGetProp(FNode,"reference");
            if(RefType == NULL)
                RefTypeValue = NOTE_REFTYPE_NONE;
            else if( g_strcmp0(RefType, "event-start") == 0 )
                RefTypeValue = NOTE_REFTYPE_EVENT_START;
            else if( g_strcmp0(RefType, "event-middle") == 0 )
                RefTypeValue = NOTE_REFTYPE_EVENT_MIDDLE;
            else if( g_strcmp0(RefType, "event-end") == 0 )
                RefTypeValue = NOTE_REFTYPE_EVENT_END;
            else if( g_strcmp0(RefType, "actor") == 0 )
                RefTypeValue = NOTE_REFTYPE_ACTOR;
            else if( g_strcmp0(RefType, "vspan") == 0 )
                RefTypeValue = NOTE_REFTYPE_VSPAN;
            else if( g_strcmp0(RefType, "boxspan") == 0 )
                RefTypeValue = NOTE_REFTYPE_BOXSPAN;
            else
            {
                g_error("Note description reference \"%s\" is not supported.\n", RefType);
                return TRUE;
            }
        
            RefId = xmlGetProp(FNode,"refid");
            switch(RefTypeValue)
            {
                case NOTE_REFTYPE_NONE:          
                break;

                case NOTE_REFTYPE_ACTOR:         
                case NOTE_REFTYPE_EVENT_START:   
                case NOTE_REFTYPE_EVENT_MIDDLE:  
                case NOTE_REFTYPE_EVENT_END:     
                case NOTE_REFTYPE_VSPAN:         
                case NOTE_REFTYPE_BOXSPAN:       
                    if(RefId == NULL)
                    {
                        g_error("This type of note reference requires a refid property.\n");
                        return TRUE;
                    }
                break;
            }
    
            NoteStr = xmlNodeGetContent(FNode);
		    normalize_content_str(NoteStr);

            sqd_layout_add_note(SL, idStr, NodeIndx, RefTypeValue, RefId, NoteStr);

            if(idStr) xmlFree(idStr);
            if(RefType) xmlFree(RefType);
            if(RefId) xmlFree(RefId);
            if(NoteStr) xmlFree(NoteStr);
		}

        xmlFree(xpathlist);
    }

	// Free up the libxml structures.
	xmlXPathFreeContext( XPath );    
}

gchar teststr[] = "test  content   string with \r\n newlines and \t\t\t tabs.";

int
main (int argc, char *argv[])
{
    double width, height;
    xmlDocPtr          SeqDoc;     
    xmlXPathContextPtr XPath;       
    xmlNodePtr         root_node;
    xmlChar           *idStr;
    xmlNodeSetPtr      nodeset;  
	xmlNodePtr         FNode;
	xmlXPathObjectPtr  xpathlist;
    guint32            NodeIndx;

    SQDLayout *SL;

	gchar *input_path  = NULL;
	gchar *output_pdf  = NULL;
	gchar *output_png  = NULL;
	gchar *output_svg  = NULL;

	GOptionContext *context;

	GOptionEntry entries[] = {
	  { "input-xml", 'i', 0, G_OPTION_ARG_STRING, &input_path, "The xml formatted sequence diagram description file.", "<filename>"},
	  { "output-pdf", 'p', 0, G_OPTION_ARG_STRING, &output_pdf, "The pdf formatted sequence diagram.", "<filename>"},
	  { "output-png", 'g', 0, G_OPTION_ARG_STRING, &output_png, "The png formatted sequence diagram.", "<filename>"},
	  { "output-svg", 's', 0, G_OPTION_ARG_STRING, &output_svg, "The svg formatted sequence diagram.", "<filename>"},
//	  { "symbol", 's', 0, G_OPTION_ARG_STRING, &symbol_path, "The symbol table file. (xml-format)", "<filename>"},
//	  { "format", 'f', 0, G_OPTION_ARG_STRING, &format_path, "The trace formatting file. (xml-format)", "<filename>"},
	  { NULL }
	};

    // Initialize threading.
    g_type_init();

	context = g_option_context_new ("- sequence diagram generation");
	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);

    // Make sure an input file was specified.
    if( input_path == NULL )
    {
        g_error("An input file is required.\n");
    }

    // Allocate a layout object to build the sequence diagram into.
    SL = sqd_layout_new();

    // Parse the input file.
    // Try to open the policy file.
    SeqDoc = xmlParseFile( input_path );    

    // Check for control file existance
    if( SeqDoc == NULL ) 
    {
        g_error("Input file could not be opened.\n");
        return TRUE;
    }

    // Grab a pointer to the root node
    root_node = xmlDocGetRootElement( SeqDoc );
	
	if (root_node == NULL)
	{
		g_error("Invalid sequence description input file -- Empty file or not XML\n");
		xmlFreeDoc( SeqDoc );
		return;
	}

    // Verify that the root node has the expected name
    if( g_strcmp0(root_node->name, "seqdraw") != 0 )
	{
		g_error("Invalid sequence description input file -- Unexpected root node.\n");
		xmlFreeDoc( SeqDoc );
		return;
	}

	// Build a context which XPath can reference from
	XPath = xmlXPathNewContext( SeqDoc );
    XPath->node = root_node;
    
    // Add the namespaces of interest.
    xmlXPathRegisterNs(XPath, "sqd", "http://nottbergbros.com/seqdraw");

    // Sequence diagram description.
    // Load Planned test cases from the expected file.
    // Query for decoder definitions.
	xpathlist = xmlXPathEvalExpression("sqd:sequence", XPath);

    g_print( "query: 0x%x, %d\n", xpathlist, xmlXPathNodeSetIsEmpty(xpathlist->nodesetval) );

    // If nodes where found then parse them.
	if( xpathlist )
	{
   		nodeset = xpathlist->nodesetval; 
	    g_print( "sequence node count: %d\n", nodeset->nodeNr );    

        if( nodeset->nodeNr == 0 )
        {
            g_error("A sequence node was not found.\n");
            return;
        }

        if( nodeset->nodeNr > 1 )
        {
            g_error("Only a single sequence node per input file is currently supported.\n");
            return;
        }

        // Parse the name and description.
        parse_name_and_description(SL, SeqDoc, nodeset->nodeTab[0]);

        // Parse the actor nodes.
        parse_actor_list( SL, SeqDoc, nodeset->nodeTab[0]);

        // Parse the event nodes.
        parse_event_list( SL, SeqDoc, nodeset->nodeTab[0]);

        // Parse the event nodes.
        parse_note_list( SL, SeqDoc, nodeset->nodeTab[0]);

        xmlFree(xpathlist);
    }

	// Free up the libxml structures.
	xmlXPathFreeContext( XPath );    

    //sqd_layout_add_actor(SL, "host", 0, "SLI-Host");
    //sqd_layout_add_actor(SL, "firmware", 1, "Firmware");
    //sqd_layout_add_actor(SL, "wire", 2, "Wire");
    //sqd_layout_add_actor(SL, "hdd", 3, "HDD");

    //sqd_layout_add_event(SL, "ev1", 0, "host", "firmware", "Bob", NULL);
    //sqd_layout_add_event(SL, "ev2", 1, "host", "port", NULL, NULL);
    //sqd_layout_add_event(SL, "ev3", 2, "port", "dma", NULL, NULL);
    //sqd_layout_add_event(SL, "ev4", 3, "port", "host", "Rosita", "Fred");
    //sqd_layout_add_event(SL, "ev5", 4, "host", "firmware", NULL, NULL);
    //sqd_layout_add_event(SL, "ev6", 4, "port", "dma", NULL, "Ted");

    //sqd_layout_add_note(SL, "note1", 0, NOTE_REFTYPE_EVENT_START, "e2", "Test Note 1");
    //sqd_layout_add_note(SL, "note2", 1, NOTE_REFTYPE_ACTOR, "firmware", "Kevin likes to put his notes over in a column on the side of the sequence diagram and then draw a line between the note and the event of interest.");
    //sqd_layout_add_note(SL, "note3", 2, NOTE_REFTYPE_EVENT_MIDDLE, "e4", "Test Note 3");

    // Cleanup
    xmlFreeDoc( SeqDoc );

    // Check if pdf should be generated.
    if( output_pdf )
    {
        sqd_layout_generate_pdf( SL, output_pdf );
    }

    // Check if pdf should be generated.
    if( output_png )
    {
        sqd_layout_generate_png( SL, output_png );
    }

    // Check if pdf should be generated.
    if( output_svg )
    {
        sqd_layout_generate_svg( SL, output_svg );
    }

    // Finish with the object.
    g_object_unref(SL);

    
    normalize_content_str(teststr);
    g_print("collapsed string: %s\n", teststr);

    // Success
    return 0;
}
