/*
*    Copyright 2009 Curtis Nottberg
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Lesser General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU Lesser General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * sqd-layout.c
 *
 * Implements a GObject representing a sequence diagram layout.
 *
 */
#include <glib.h>

// These functions should probably be replaced with glib equivalents.
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xmlreader.h>

#include "sqd-layout.h"


// Data structures
#include <cairo.h>
#include <cairo/cairo-pdf.h>
#include <cairo/cairo-svg.h>

#include <math.h>
#include <pango/pangocairo.h>

typedef struct SDPresentationParameter
{
    gchar *ParamStr;
    gchar *ClassStr;
    gchar *ValueStr;
}SQD_P_PARAM;

typedef struct SDColorStruct
{
    gdouble  Red;
    gdouble  Green;
    gdouble  Blue;
    gdouble  Alpha;
}SQD_COLOR;

typedef struct SeqDrawBox
{
    double Top;
    double Bottom;
    double Start;
    double End;
}SQD_BOX;

typedef struct SeqTxtRecord
{
    char   *Str;
    double  Width;
    double  Height;
}SQD_TXT;

enum SeqDrawObjectTypeEnum
{
    SDOBJ_ACTOR,
    SDOBJ_EVENT,
    SDOBJ_NOTE,
    SDOBJ_AREGION,
    SDOBJ_BREGION
};

typedef struct SeqDrawObjectHdr
{
    guint8  Type;
    guint8  Index;
    gchar  *IdStr; 
    gchar  *ClassStr;
}SQD_OBJ;

typedef struct SeqDrawActorRecord
{
    SQD_OBJ hdr;

    SQD_TXT Name;

    SQD_BOX BoundsBox;
    SQD_BOX NameBox;
    SQD_BOX BaselineBox;
    SQD_BOX StemBox;

}SQD_ACTOR;

#define ACTOR_INDEX_LEFT_EDGE  0xFF
#define ACTOR_INDEX_RIGHT_EDGE 0xFE

enum EventArrowDirectionIndicators
{
    ARROWDIR_EXTERNAL_TO,        // External event to an actor left
    ARROWDIR_EXTERNAL_FROM,      // Actor event to external
    ARROWDIR_LEFT_TO_RIGHT,      // An event moving to the right
    ARROWDIR_RIGHT_TO_LEFT,      // An event moving to the left
    ARROWDIR_STEP,               // An internal actor event
};

typedef struct SeqDrawEventRecord
{
    SQD_OBJ hdr;

    guint8  StartActorIndx;
    guint8  EndActorIndx;

    guint8  ArrowDir;

    double  Height;

    SQD_TXT UpperText;
    SQD_TXT LowerText;

    SQD_BOX EventBox;

    SQD_BOX UpperTextBox;
    SQD_BOX StemBox;
    SQD_BOX LowerTextBox;
}SQD_EVENT;

typedef struct SeqDrawEventRecordLayer
{
    guint32  UsedMask;

    gboolean RegularLayer;   // This layer is occupied by regular events.
    gboolean StepLayer;      // This layer is occupied by step events.  
    gboolean ExternalLayer;  // This layer is occupied by external events.

    double   Height;

    SQD_BOX  LayerBox;

    guint8   EventCnt;
    GList   *Events;
}SQD_EVENT_LAYER;

typedef struct SeqDrawActorRegionRecord
{
    SQD_OBJ hdr;

    SQD_ACTOR *ActorRef;
    SQD_EVENT *SEventRef;
    SQD_EVENT *EEventRef;

    SQD_BOX BoundsBox;
}SQD_ACTOR_REGION;

typedef struct SeqDrawBoxRegionRecord
{
    SQD_OBJ hdr;

    SQD_ACTOR *SActorRef;
    SQD_ACTOR *EActorRef;
    SQD_EVENT *SEventRef;
    SQD_EVENT *EEventRef;

    SQD_BOX BoundsBox;
}SQD_BOX_REGION;

typedef struct SeqDrawNoteRecord
{
    SQD_OBJ hdr;

    SQD_OBJ *RefObj;

    double  Height;

    SQD_TXT Text;

    SQD_BOX BoundsBox;

    guint8  ReferenceType;

    double  RefFirstTop;
    double  RefFirstStart;
    double  RefLastTop;
    double  RefLastStart;

}SQD_NOTE;

// Prototypes
static void draw_text (cairo_t *cr);
static gchar* sqd_layout_get_pparam( SQDLayout *sb, gchar *IdStr, gchar *ClassStr );
static void sqd_layout_draw_actor ( SQDLayout *sb, int ActorIndex, char *ActorTitle);
static void sqd_layout_draw_arrow ( SQDLayout *sb, int EventIndex, int StartActorIndex, int EndActorIndex, char *TopText, char *BottomText);
static void debug_box_print(char *BoxName, SQD_BOX *Box);
static int sqd_layout_measure_text( SQDLayout *sb, SQD_TXT *Text, double Width);
static double sqd_layout_arrange_actors( SQDLayout *sb );
static void sqd_layout_get_actor_point( SQDLayout *sb, SQD_OBJ *RefObj, double *Top, double *Start );
static double sqd_layout_arrange_notes( SQDLayout *sb );
static int sqd_layout_arrange_events( SQDLayout *sb );
static void sqd_layout_get_event_point( SQDLayout *sb, SQD_OBJ *RefObj, int RefType, double *Top, double *Start );
static double sqd_layout_arrange_notes_references( SQDLayout *sb );
static int sqd_layout_arrange_diagram( SQDLayout *sb );
static void sqd_layout_draw_text( SQDLayout *sb, SQD_TXT *Text, double Width );
static void sqd_layout_draw_actors( SQDLayout *sb );
static void sqd_layout_draw_events( SQDLayout *sb );
static void sqd_layout_draw_notes( SQDLayout *sb );
static void sqd_layout_draw_note_references( SQDLayout *sb );
static void sqd_layout_draw_aregions( SQDLayout *sb );
static void sqd_layout_draw_bregions( SQDLayout *sb );
static int sqd_layout_draw_diagram( SQDLayout *sb );

// Object start
#define SQD_LAYOUT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_TYPE_SQD_LAYOUT, SQDLayoutPrivate))

G_DEFINE_TYPE (SQDLayout, sqd_layout, G_TYPE_OBJECT);

typedef struct _SQDLayoutPrivate SQDLayoutPrivate;
struct _SQDLayoutPrivate
{
    // Document Specs
    gdouble  Width;
    gdouble  Height;

    gdouble  Margin;
    gdouble  TextPad;
    gdouble  ElementPad;

    gdouble  LineWidth;

    gdouble  MinEventPad;
    gdouble  ArrowWidth;
    gdouble  ArrowLength;

    gdouble  NoteBoxWidth;

    SQD_TXT Title;
    SQD_BOX TitleBar;

    SQD_TXT Description;

    // High Level Layout regions
    SQD_BOX TitleBox;
    SQD_BOX DescriptionBox;
    SQD_BOX ActorBox;
    SQD_BOX SeqBox;
    SQD_BOX NoteBox;

    // Actor Stats
    gint    MaxActorIndex;
    gdouble MaxActorHeight;
    gdouble ActorWidth;

    // Event Stats
    gint MaxEventIndex;

    // Note Stats
    gint MaxNoteIndex;

    // Actor, Event, Note Lists
    GPtrArray *Notes;
    GPtrArray *Actors;
    GPtrArray *ActorRegions;
    GPtrArray *BoxRegions;
    GArray    *EventLayers;

    //GList  *Events;
    cairo_surface_t *surface;
    cairo_t         *cr;

    gboolean      dispose_has_run;

    // Keep a hash table of assigned IDs
    GHashTable *IdTable;

    // Keep a hash table of presentation parameters
    GHashTable *PTable;

    // Presentation Parameters 
    gchar       *FontStr;

    SQD_COLOR   TextColor;
    SQD_COLOR   LineColor;
    SQD_COLOR   FillColor;
    SQD_COLOR   StemColor;
};

/* GObject callbacks */
static void sqd_layout_set_property (GObject 	 *object,
					    guint	  prop_id,
					    const GValue *value,
					    GParamSpec	 *pspec);
static void sqd_layout_get_property (GObject	 *object,
					    guint	  prop_id,
					    GValue	 *value,
					    GParamSpec	 *pspec);

static GObjectClass *parent_class = NULL;

static void
sqd_layout_dispose (GObject *obj)
{
    SQDLayout        *self = (SQDLayout *)obj;
    SQDLayoutPrivate *priv;

	priv = SQD_LAYOUT_GET_PRIVATE(self);

    if(priv->dispose_has_run)
    {
        /* If dispose did already run, return. */
        return;
    }

    /* Make sure dispose does not run twice. */
    priv->dispose_has_run = TRUE;

    /* 
    * In dispose, you are supposed to free all types referenced from this
    * object which might themselves hold a reference to self. Generally,
    * the most simple solution is to unref all members on which you own a 
    * reference.
    */

    /* Chain up to the parent class */
    G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
sqd_layout_finalize (GObject *obj)
{
    SQDLayout *self = (SQDLayout *)obj;

    /* Chain up to the parent class */
    G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
sqd_layout_class_init (SQDLayoutClass *class)
{
	GObjectClass *o_class;
	int error;
	
	o_class = G_OBJECT_CLASS (class);

    o_class->dispose  = sqd_layout_dispose;
    o_class->finalize = sqd_layout_finalize;

	/*
	o_class->set_property = sqd_layout_browser_set_property;
	o_class->get_property = sqd_layout_browser_get_property;
	*/
	
	/* create signals */

	class->req_complete_id = g_signal_new (
			"request-complete",
			G_OBJECT_CLASS_TYPE (o_class),
			G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET (SQDLayoutClass, req_complete),
			NULL, NULL,
			g_cclosure_marshal_VOID__POINTER,
			G_TYPE_NONE,
			1, G_TYPE_POINTER);

    parent_class = g_type_class_peek_parent (class);      
	g_type_class_add_private (o_class, sizeof (SQDLayoutPrivate));
}



static void
sqd_layout_init (SQDLayout *sb)
{
	SQDLayoutPrivate *priv;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    priv->Width        = 8*72;
    priv->Height       = 11*72;

    priv->LineWidth      = 2;

    priv->SeqBox.Start   = 72/2;
    priv->SeqBox.End     = ((8*72) - 72) / 4;
    priv->SeqBox.Top     = 72;
    priv->SeqBox.Bottom  = priv->Height - 72;

    priv->Margin       = 0.5 * 72;
    priv->TextPad      = 2;
    priv->ElementPad   = 2;

    priv->MinEventPad  = 20;
    priv->ArrowWidth   = 6;
    priv->ArrowLength  = 6;

    priv->NoteBoxWidth   = 2*72;

    //priv->EventHeight  = (priv->Height / (72 * 0.33333));

    priv->MaxActorIndex  = 0;
    priv->MaxActorHeight = 0;
    priv->ActorWidth     = 0;

    priv->Actors = g_ptr_array_new();

    priv->MaxEventIndex  = 0;

    priv->EventLayers = g_array_new(FALSE, TRUE, sizeof (SQD_EVENT_LAYER));

    priv->MaxNoteIndex   = 0;
    priv->Notes = g_ptr_array_new();

    priv->ActorRegions = g_ptr_array_new();
    priv->BoxRegions   = g_ptr_array_new();

    priv->Title.Str       = NULL;
    priv->Description.Str = NULL;

    priv->surface = NULL;
    priv->cr      = NULL;
        
    priv->IdTable = g_hash_table_new(g_str_hash, g_str_equal);

    priv->PTable = g_hash_table_new(g_str_hash, g_str_equal);

    priv->dispose_has_run = FALSE;

    sqd_layout_set_presentation_parameter(sb, "font", "Times 10", NULL);
    sqd_layout_set_presentation_parameter(sb, "description.font", "Courier 8", NULL);
    sqd_layout_set_presentation_parameter(sb, "title.font", "Impact 10", NULL);
    sqd_layout_set_presentation_parameter(sb, "note.font", "Times 6", NULL);

    //sqd_layout_set_presentation_parameter(sb, "text.color", "0,0,0,255", NULL);
    sqd_layout_set_presentation_parameter(sb, "text.color", "95,158,160,255", NULL);
    sqd_layout_set_presentation_parameter(sb, "line.color", "0,0,0,255", NULL);
    sqd_layout_set_presentation_parameter(sb, "fill.color", "255,228,196,255", NULL);

    sqd_layout_set_presentation_parameter(sb, "actor.stem.color", "128,128,128,128", NULL);
    sqd_layout_set_presentation_parameter(sb, "noteref.stem.color", "100,100,100,128", NULL);

    sqd_layout_set_presentation_parameter(sb, "actor-region.fill.color", "255,127,80,100", NULL);
    sqd_layout_set_presentation_parameter(sb, "box-region.fill.color", "205,92,92,100", NULL);

}

static gchar* 
sqd_layout_get_pparam( SQDLayout *sb, gchar *ParamStr, gchar *ClassStr )
{
	SQDLayoutPrivate *priv;
    gchar            *PStr;
    SQD_P_PARAM      *PParam;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Build the Parameter ID String
    if(ClassStr)
        PStr = g_strdup_printf("%s.%s", ClassStr, ParamStr);
    else
        PStr = g_strdup(ParamStr);

    // Check if the Parameter exists
    PParam = g_hash_table_lookup(priv->PTable, PStr);
    if( PParam != NULL )
    {
        g_print("pparam( %s ) = %s\n", PStr, PParam->ValueStr);
        g_free(PStr);
        return PParam->ValueStr;
    }

    // Cleanup
    g_free(PStr);

    // Try just the ParamStr incase the class didn't have the string defined.
    PParam = g_hash_table_lookup(priv->PTable, ParamStr);
    if( PParam != NULL )
    {
        g_print("pparam( %s ) = %s\n", ParamStr, PParam->ValueStr);
        return PParam->ValueStr;
    }

    // Parameter was not found
    g_print("pparam( %s ) = %s\n", ParamStr, "NULL");
    return NULL;
}

static gboolean
sqd_layout_process_color_str( SQDLayout *sb, gchar *ColorStr, SQD_COLOR *Color )
{
    gdouble R;
    gdouble G;
    gdouble B;
    gdouble A;
    gchar   **TList;

    // Default the color to opaque black
    Color->Red   = 0;
    Color->Green = 0;
    Color->Blue  = 0;
    Color->Alpha = 1.0;

    // Attempt to decode the string.
    TList = g_strsplit(ColorStr, ",", 4);

    // Decode the Red Value
    if(TList[0] == NULL)
        return TRUE;
    R = strtol(TList[0], NULL, 0);
    if( (R > 255.0) || (R < 0.0) ) return TRUE;
    R /= 255.0;

    // Decode the Green Value
    if(TList[1] == NULL)
        return TRUE;
    G = strtol(TList[1], NULL, 0);
    if( (G > 255.0) || (G < 0.0) ) return TRUE;
    G /= 255.0;

    // Decode the Blue Value
    if(TList[2] == NULL)
        return TRUE;
    B = strtol(TList[2], NULL, 0);
    if( (B > 255.0) || (B < 0.0) ) return TRUE;
    B /= 255.0;

    // Decode the alpha value
    if(TList[3] == NULL)
        return TRUE;
    A = strtol(TList[3], NULL, 0);
    if( (A > 255.0) || (A < 0.0) ) return TRUE;
    A /= 255.0;

    // Free the tokens
    g_strfreev(TList);

    g_print("RGBA: %g, %g, %g, %g\n", R, G, B, A);

    // Default the color to opaque black
    Color->Red   = R;
    Color->Green = G;
    Color->Blue  = B;
    Color->Alpha = A;

    // Done
    return FALSE;

}

static void
sqd_layout_use_default_presentation( SQDLayout *sb )
{
	SQDLayoutPrivate *priv;
    gchar *TmpStr;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Use the default font
    priv->FontStr = sqd_layout_get_pparam( sb, "font", NULL );

    // Process a color strings.
    TmpStr = sqd_layout_get_pparam(sb, "text.color", NULL);
    sqd_layout_process_color_str(sb, TmpStr, &priv->TextColor);

    TmpStr = sqd_layout_get_pparam(sb, "line.color", NULL);
    sqd_layout_process_color_str(sb, TmpStr, &priv->LineColor);
    sqd_layout_process_color_str(sb, TmpStr, &priv->StemColor);

    TmpStr = sqd_layout_get_pparam(sb, "fill.color", NULL);
    sqd_layout_process_color_str(sb, TmpStr, &priv->FillColor);

}

static void
sqd_layout_use_title_presentation( SQDLayout *sb )
{
	SQDLayoutPrivate *priv;
    gchar *TmpStr;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // First look for a specific font for the title block
    priv->FontStr = sqd_layout_get_pparam( sb, "title.font", NULL );
    if( priv->FontStr == NULL )
        priv->FontStr = sqd_layout_get_pparam( sb, "font", NULL );

    // Title color 
    TmpStr = sqd_layout_get_pparam(sb, "title.color", NULL);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "fill.color", NULL);

    sqd_layout_process_color_str(sb, TmpStr, &priv->FillColor);
   
}

static void
sqd_layout_use_description_presentation( SQDLayout *sb )
{
	SQDLayoutPrivate *priv;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // First look for a specific font for the description block
    priv->FontStr = sqd_layout_get_pparam( sb, "description.font", NULL );

    // Fall back to the default font.
    if( priv->FontStr == NULL )
        priv->FontStr = sqd_layout_get_pparam( sb, "font", NULL );

}

static void
sqd_layout_use_actor_presentation( SQDLayout *sb, gchar *ClassStr )
{
	SQDLayoutPrivate *priv;
    gchar            *TmpStr;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // First look for a specific font for the actor block, in decreasing specificity.
    priv->FontStr = sqd_layout_get_pparam( sb, "actor.font", ClassStr );
    if( priv->FontStr == NULL )
        priv->FontStr = sqd_layout_get_pparam( sb, "font", ClassStr );
    if( priv->FontStr == NULL )
        priv->FontStr = sqd_layout_get_pparam( sb, "actor.font", NULL );
    if( priv->FontStr == NULL )
        priv->FontStr = sqd_layout_get_pparam( sb, "font", NULL );

    // Look for a specfic fill color. 
    TmpStr = sqd_layout_get_pparam(sb, "actor.fill.color", ClassStr);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "fill.color", ClassStr);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "actor.fill.color", NULL);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "fill.color", NULL);

    sqd_layout_process_color_str(sb, TmpStr, &priv->FillColor);

    // Look for a specfic stem color. 
    TmpStr = sqd_layout_get_pparam(sb, "actor.stem.color", ClassStr);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "line.color", ClassStr);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "actor.stem.color", NULL);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "line.color", NULL);

    sqd_layout_process_color_str(sb, TmpStr, &priv->StemColor);

}

static void
sqd_layout_use_event_presentation( SQDLayout *sb, gchar *ClassStr )
{
	SQDLayoutPrivate *priv;
    gchar            *TmpStr;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // First look for a specific font for the actor block, in decreasing specificity.
    priv->FontStr = sqd_layout_get_pparam( sb, "event.font", ClassStr );
    if( priv->FontStr == NULL )
        priv->FontStr = sqd_layout_get_pparam( sb, "font", ClassStr );
    if( priv->FontStr == NULL )
        priv->FontStr = sqd_layout_get_pparam( sb, "event.font", NULL );
    if( priv->FontStr == NULL )
        priv->FontStr = sqd_layout_get_pparam( sb, "font", NULL );

    // Look for a specfic stem color. 
    TmpStr = sqd_layout_get_pparam(sb, "event.stem.color", ClassStr);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "line.color", ClassStr);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "event.stem.color", NULL);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "line.color", NULL);

    sqd_layout_process_color_str(sb, TmpStr, &priv->StemColor);

}

static void
sqd_layout_use_note_presentation( SQDLayout *sb, gchar *ClassStr )
{
	SQDLayoutPrivate *priv;
    gchar            *TmpStr;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // First look for a specific font for the actor block, in decreasing specificity.
    priv->FontStr = sqd_layout_get_pparam( sb, "note.font", ClassStr );
    if( priv->FontStr == NULL )
        priv->FontStr = sqd_layout_get_pparam( sb, "font", ClassStr );
    if( priv->FontStr == NULL )
        priv->FontStr = sqd_layout_get_pparam( sb, "note.font", NULL );
    if( priv->FontStr == NULL )
        priv->FontStr = sqd_layout_get_pparam( sb, "font", NULL );

    // Look for a specfic fill color. 
    TmpStr = sqd_layout_get_pparam(sb, "note.fill.color", ClassStr);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "fill.color", ClassStr);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "note.fill.color", NULL);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "fill.color", NULL);

    sqd_layout_process_color_str(sb, TmpStr, &priv->FillColor);

}

static void
sqd_layout_use_noteref_presentation( SQDLayout *sb, gchar *ClassStr )
{
	SQDLayoutPrivate *priv;
    gchar            *TmpStr;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Look for a specfic stem color. 
    TmpStr = sqd_layout_get_pparam(sb, "noteref.stem.color", ClassStr);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "line.color", ClassStr);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "noteref.stem.color", NULL);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "line.color", NULL);

    sqd_layout_process_color_str(sb, TmpStr, &priv->StemColor);

}

static void
sqd_layout_use_aregion_presentation( SQDLayout *sb, gchar *ClassStr )
{
	SQDLayoutPrivate *priv;
    gchar            *TmpStr;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Look for a specfic fill color. 
    TmpStr = sqd_layout_get_pparam(sb, "actor-region.fill.color", ClassStr);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "fill.color", ClassStr);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "actor-region.fill.color", NULL);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "fill.color", NULL);

    sqd_layout_process_color_str(sb, TmpStr, &priv->FillColor);

}

static void
sqd_layout_use_bregion_presentation( SQDLayout *sb, gchar *ClassStr )
{
	SQDLayoutPrivate *priv;
    gchar            *TmpStr;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Look for a specfic fill color. 
    TmpStr = sqd_layout_get_pparam(sb, "box-region.fill.color", ClassStr);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "fill.color", ClassStr);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "box-region.fill.color", NULL);
    if( TmpStr == NULL )
        TmpStr = sqd_layout_get_pparam(sb, "fill.color", NULL);

    sqd_layout_process_color_str(sb, TmpStr, &priv->FillColor);
}



static void
sqd_layout_draw_rounded_rec( SQDLayout *sb, gdouble x, gdouble y, gdouble w, gdouble h, gdouble r)
{
	SQDLayoutPrivate *priv;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // "Draw a rounded rectangle"
    //   A****BQ
    //  H      C
    //  *      *
    //  G      D
    //   F****E
    cairo_move_to(priv->cr, x+r,y);                     // Move to A
    cairo_line_to(priv->cr,x+w-r,y);                    // Straight line to B
    cairo_curve_to(priv->cr,x+w,y,x+w,y,x+w,y+r);       // Curve to C, Control points are both at Q
    cairo_line_to(priv->cr,x+w,y+h-r);                  // Move to D
    cairo_curve_to(priv->cr,x+w,y+h,x+w,y+h,x+w-r,y+h); // Curve to E
    cairo_line_to(priv->cr,x+r,y+h);                    // Line to F
    cairo_curve_to(priv->cr,x,y+h,x,y+h,x,y+h-r);       // Curve to G
    cairo_line_to(priv->cr,x,y+r);                      // Line to H
    cairo_curve_to(priv->cr,x,y,x,y,x+r,y);             // Curve to A
}

static void
debug_box_print(char *BoxName, SQD_BOX *Box)
{
    printf("%s: %g %g %g %g\n", BoxName, Box->Start, Box->End, Box->Top, Box->Bottom);
}



static int
sqd_layout_measure_text( SQDLayout *sb, SQD_TXT *Text, double Width)
{
	SQDLayoutPrivate *priv;
    PangoLayout *layout;
    PangoFontDescription *desc;
    int pwidth, pheight;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Create a PangoLayout, set the font and text 
    layout = pango_cairo_create_layout (priv->cr);
  
    if( Width )
    {
        pango_layout_set_width (layout, (Width * PANGO_SCALE));
        pango_layout_set_wrap (layout, PANGO_WRAP_WORD);
    }

    desc = pango_font_description_from_string( priv->FontStr );
    pango_layout_set_font_description (layout, desc);
    pango_font_description_free (desc);

    pango_layout_set_markup (layout, Text->Str, -1);

    pango_layout_get_size (layout, &pwidth, &pheight);

    Text->Width  = ((double)pwidth  / PANGO_SCALE); 
    Text->Height = ((double)pheight / PANGO_SCALE); 

    // free the layout object 
    g_object_unref (layout);

}

// sqd_layout_get_pparam( sb, "font", NULL)

static double
sqd_layout_arrange_actors( SQDLayout *sb )
{
	SQDLayoutPrivate *priv;
    GList *Element;
    SQD_ACTOR *Actor;
    int i;
    double ActorTop;
    double ActorTextWidth;
    double ActorMaxTextWidth;
    double EventTop;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Top of the event box.
    EventTop = 0;

    // The Actor Box should now contain the space allocated for actor columns. 
    ActorTop   = priv->ActorBox.Top + priv->ElementPad;
    priv->ActorWidth = (priv->ActorBox.End - priv->ActorBox.Start) / (priv->MaxActorIndex + 1.0);

    // Have the Actor Text take up the middle two thirds of the width.
    ActorTextWidth = (priv->ActorWidth * 2.0)/3.0;

    printf("Actor Widths: %d %g %g\n", priv->MaxActorIndex+1, priv->ActorWidth, ActorTextWidth); 

    ActorMaxTextWidth = 0;

    // Determine the width and height of the actors text.
    for (i = 0; i <= priv->MaxActorIndex; i++)
    {
        Actor = g_ptr_array_index(priv->Actors, i);

        // Setup the parameters for the title bar
        sqd_layout_use_actor_presentation(sb, Actor->hdr.ClassStr);

        sqd_layout_measure_text(sb, &Actor->Name, ActorTextWidth);

        printf("Pango Actor Extents: %g %g\n", Actor->Name.Width, Actor->Name.Height); 

        if( Actor->Name.Height > priv->MaxActorHeight )
            priv->MaxActorHeight = Actor->Name.Height;   

        if( Actor->Name.Width > ActorMaxTextWidth )
            ActorMaxTextWidth = Actor->Name.Width;       

        // Restore default presentation
        sqd_layout_use_default_presentation(sb); 
    
    }

    printf("Pango Actor Maxs: %g %g\n", priv->MaxActorHeight, ActorMaxTextWidth); 

    // Layout the Actors
    for (i = 0; i <= priv->MaxActorIndex; i++)
    {
        Actor = g_ptr_array_index(priv->Actors, i);

        // Setup the parameters for the title bar
        sqd_layout_use_actor_presentation(sb, Actor->hdr.ClassStr);

        Actor->BoundsBox.Top    = ActorTop;
        Actor->BoundsBox.Bottom = priv->ActorBox.Bottom;
        Actor->BoundsBox.Start  = priv->ActorBox.Start + (i * priv->ActorWidth);
        Actor->BoundsBox.End    = Actor->BoundsBox.Start + priv->ActorWidth;

        debug_box_print("Bounds Box", &Actor->BoundsBox);

        Actor->NameBox.Top     = ActorTop;
        Actor->NameBox.Bottom  = ActorTop + priv->MaxActorHeight + (2 * priv->TextPad);
        Actor->NameBox.Start   = Actor->BoundsBox.Start + (priv->ActorWidth / 2.0) - (ActorMaxTextWidth / 2.0) - priv->TextPad;
        Actor->NameBox.End     = Actor->NameBox.Start + ActorMaxTextWidth + (2 * priv->TextPad);

        debug_box_print("Name Box", &Actor->NameBox);

        Actor->BaselineBox.Top     = Actor->NameBox.Bottom;
        Actor->BaselineBox.Bottom  = Actor->BaselineBox.Top + priv->LineWidth;
        Actor->BaselineBox.Start   = Actor->NameBox.Start;
        Actor->BaselineBox.End     = Actor->NameBox.End;

        Actor->StemBox.Top         = Actor->BaselineBox.Top;
        Actor->StemBox.Bottom      = priv->ActorBox.Bottom - priv->ElementPad;
        Actor->StemBox.Start       = Actor->NameBox.Start + ((Actor->NameBox.End - Actor->NameBox.Start)/ 2.0) - priv->LineWidth;
        Actor->StemBox.End         = Actor->StemBox.Start + (2 * priv->LineWidth);

        if( (Actor->StemBox.Top + priv->ElementPad) > EventTop )
            EventTop = (Actor->StemBox.Top + priv->ElementPad);

        // Restore default presentation
        sqd_layout_use_default_presentation(sb);
    }

    return EventTop;

}

static void 
sqd_layout_get_actor_point( SQDLayout *sb, SQD_OBJ *RefObj, double *Top, double *Start )
{
	SQDLayoutPrivate *priv;
    GList *Element;
    SQD_ACTOR *Actor;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // default the point
    *Top   = 0;
    *Start = 0;

    // Look up the actor element
    Actor = (SQD_ACTOR *)RefObj;

    if( (Actor == NULL) || (Actor->hdr.Type != SDOBJ_ACTOR) )
        return;

    // Get a point at the baseline and stem intersection.
    *Top   = Actor->BaselineBox.Top - (2.0*priv->LineWidth);
    *Start = Actor->BaselineBox.End - (2.0*priv->LineWidth);
}


static double
sqd_layout_arrange_notes( SQDLayout *sb )
{
	SQDLayoutPrivate *priv;
    SQD_NOTE *Note;
    int i;
    double NoteTop;
    double NoteTextWidth;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // The Note Box should now contain the space allocated for Note columns. 
    NoteTop   = priv->NoteBox.Top + priv->ElementPad;
    NoteTextWidth = priv->NoteBoxWidth - (2 * priv->TextPad);

    // Determine the height of the Note text.
    for (i = 0; i <= priv->MaxNoteIndex; i++)
    {
        Note = g_ptr_array_index(priv->Notes, i);

        // Setup the parameters
        sqd_layout_use_note_presentation(sb, Note->hdr.ClassStr);
        
        sqd_layout_measure_text(sb, &Note->Text, NoteTextWidth);

        printf("Pango Note Extents: %g %g\n", Note->Text.Width, Note->Text.Height);      

        Note->BoundsBox.Top    = NoteTop;
        Note->BoundsBox.Bottom = NoteTop + Note->Text.Height + (2 * priv->TextPad);
        Note->BoundsBox.Start  = priv->NoteBox.Start;
        Note->BoundsBox.End    = priv->NoteBox.End;

        debug_box_print("Note Box", &Note->BoundsBox);

        NoteTop = (Note->BoundsBox.Bottom + priv->ElementPad);

        // Restore default presentation
        sqd_layout_use_default_presentation(sb);

    }

    return 0;
}



static int
sqd_layout_arrange_events( SQDLayout *sb )
{
	SQDLayoutPrivate *priv;
    GList           *Element;
    SQD_EVENT_LAYER *Layer;
    SQD_EVENT       *Event;
    SQD_ACTOR       *StartActor, *EndActor;
    int i;
    double EventTop;
    double EventWidth;
//    double ActorTextWidth;
    double EventMaxTextWidth;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // The Event Box should now contain the space allotted for laying out events. 
    EventTop   = priv->SeqBox.Top;

    // Cycle through the event layers in sequencial order to layout each one.
    for (i = 0; i <= priv->MaxEventIndex; i++)
    {
        Layer = &g_array_index(priv->EventLayers, SQD_EVENT_LAYER, i);

        // Layout each seperate event in this layer
        Element = g_list_first(Layer->Events);
        while( Element )
        {
            Event = Element->data;

            // Setup the parameters
            sqd_layout_use_event_presentation(sb, Event->hdr.ClassStr);

            // Calculate the arrow length so that available space for text layout can be calculated.
            switch ( Event->ArrowDir )
            {
                case ARROWDIR_EXTERNAL_TO:
                case ARROWDIR_EXTERNAL_FROM:

                    StartActor = g_ptr_array_index(priv->Actors, Event->StartActorIndx);

                    if( Event->ArrowDir == ARROWDIR_EXTERNAL_FROM )
                    {
                        Event->StemBox.Start   = priv->ActorBox.Start;
                        Event->StemBox.End     = StartActor->StemBox.Start; // - (priv->LineWidth/2.0);
                    }
                    else
                    {
                        Event->StemBox.End   = StartActor->StemBox.Start - (priv->LineWidth/2.0);
                        Event->StemBox.Start = priv->ActorBox.Start;
                    }

                    Event->EventBox.Top    = EventTop;
                    Event->EventBox.Start  = Event->StemBox.Start;
                    Event->EventBox.End    = Event->StemBox.End;

                    EventMaxTextWidth = ((Event->StemBox.End - Event->StemBox.Start) - ((2 * priv->TextPad) - (2 * priv->ArrowLength)));
                    Event->Height = 0;

                    if( Event->UpperText.Str )
                    {
                        sqd_layout_measure_text(sb, &Event->UpperText, EventMaxTextWidth);

                        printf("Pango Event Upper Extents: %g %g\n", Event->UpperText.Width, Event->UpperText.Height); 

                        if( ((2 * priv->TextPad) + Event->UpperText.Height) > priv->MinEventPad )
                            Event->Height += (2 * priv->TextPad) + Event->UpperText.Height;
                        else
                            Event->Height += priv->MinEventPad;

                        Event->UpperTextBox.Top     = (Event->EventBox.Top + Event->Height) - priv->TextPad - Event->UpperText.Height;
                        Event->UpperTextBox.Bottom  = Event->UpperTextBox.Top + Event->UpperText.Height;
                        Event->UpperTextBox.Start   = priv->ActorBox.Start; // Event->StemBox.Start + ((Event->StemBox.End - Event->StemBox.Start)/2.0) - (Event->UpperText.Width/2.0);
                        Event->UpperTextBox.End     = Event->UpperTextBox.Start + Event->UpperText.Width;
                    }
                    else
                    {
                        Event->Height += priv->MinEventPad;
                    }

                    // Layout the rest of the boxes for the event.
                    Event->StemBox.Top     = Event->EventBox.Top + Event->Height;
                    Event->StemBox.Bottom  = Event->StemBox.Top + priv->LineWidth;

                    Event->Height += priv->LineWidth;
                    Event->Height += priv->MinEventPad;

                    // Update the EventBox Bottom
                    Event->EventBox.Bottom = Event->EventBox.Top + Event->Height;

                    // The layer height is the maximum height of any event.
                    if( Layer->Height < Event->Height )
                        Layer->Height = Event->Height;

                break;

                case ARROWDIR_STEP:

                    EventMaxTextWidth =  (3.0*(priv->ActorWidth/4.0)) - (2 * priv->TextPad);

                    if( Event->UpperText.Str )
                    {
                        sqd_layout_measure_text(sb, &Event->UpperText, EventMaxTextWidth);

                        printf("Pango Event Upper Extents: %g %g\n", Event->UpperText.Width, Event->UpperText.Height); 

                        if( ((2 * priv->TextPad) + Event->UpperText.Height) > priv->MinEventPad )
                            Event->Height += (2 * priv->TextPad) + Event->UpperText.Height;
                        else
                            Event->Height += priv->MinEventPad;
                    }
                    else
                    {
                        Event->Height += priv->MinEventPad + 20;
                    }

                    Event->Height += priv->LineWidth;

                    StartActor = g_ptr_array_index(priv->Actors, Event->StartActorIndx);
                    
                    Event->StemBox.Start   = StartActor->StemBox.Start + (priv->LineWidth*2);
                    Event->StemBox.End     = StartActor->StemBox.Start + priv->ActorWidth/4.0;
                    
                    Event->EventBox.Top    = EventTop;
                    Event->EventBox.Start  = Event->StemBox.Start;
                    Event->EventBox.End    = Event->StemBox.End + Event->UpperText.Width + (2*priv->TextPad);

                    EventMaxTextWidth = ((Event->EventBox.End - Event->EventBox.Start) - ((2 * priv->TextPad) - (2 * priv->ArrowLength)));

                    // Layout the rest of the boxes for the event.
                    Event->StemBox.Top     = Event->EventBox.Top + priv->LineWidth;
                    Event->EventBox.Bottom = Event->EventBox.Top + Event->Height;
                    Event->StemBox.Bottom  = Event->EventBox.Bottom - priv->LineWidth;

                    if( Event->UpperText.Str )
                    {
                        Event->UpperTextBox.Top     = (Event->EventBox.Top + (Event->Height/2.0)) - (Event->UpperText.Height/2.0);
                        Event->UpperTextBox.Bottom  = Event->UpperTextBox.Top + Event->UpperText.Height;
                        Event->UpperTextBox.Start   = Event->StemBox.End + priv->TextPad;
                        Event->UpperTextBox.End     = Event->EventBox.End;
                    }
 
                    // The layer height is the maximum height of any event.
                    if( Layer->Height < Event->Height )
                        Layer->Height = Event->Height;

                break;

                case ARROWDIR_LEFT_TO_RIGHT:
                case ARROWDIR_RIGHT_TO_LEFT:

                    StartActor = g_ptr_array_index(priv->Actors, Event->StartActorIndx);
                    EndActor   = g_ptr_array_index(priv->Actors, Event->EndActorIndx);

                    if( Event->ArrowDir == ARROWDIR_LEFT_TO_RIGHT )
                    {
                        Event->StemBox.Start   = StartActor->StemBox.Start + (priv->LineWidth/2.0);
                        Event->StemBox.End     = EndActor->StemBox.Start - (priv->LineWidth/2.0);
                    }
                    else
                    {
                        Event->StemBox.Start   = EndActor->StemBox.Start + (priv->LineWidth*3.0/2.0);
                        Event->StemBox.End     = StartActor->StemBox.Start;
                    }

                    Event->EventBox.Top    = EventTop;
                    Event->EventBox.Start  = Event->StemBox.Start;
                    Event->EventBox.End    = Event->StemBox.End;

                    EventMaxTextWidth = ((Event->StemBox.End - Event->StemBox.Start) - ((2 * priv->TextPad) - (2 * priv->ArrowLength)));
                    Event->Height = 0;

                    if( Event->UpperText.Str )
                    {
                        sqd_layout_measure_text(sb, &Event->UpperText, EventMaxTextWidth);

                        printf("Pango Event Upper Extents: %g %g\n", Event->UpperText.Width, Event->UpperText.Height); 

                        if( ((2 * priv->TextPad) + Event->UpperText.Height) > priv->MinEventPad )
                            Event->Height += (2 * priv->TextPad) + Event->UpperText.Height;
                        else
                            Event->Height += priv->MinEventPad;

                        Event->UpperTextBox.Top     = (Event->EventBox.Top + Event->Height) - priv->TextPad - Event->UpperText.Height;
                        Event->UpperTextBox.Bottom  = Event->UpperTextBox.Top + Event->UpperText.Height;
                        Event->UpperTextBox.Start   = Event->StemBox.Start + ((Event->StemBox.End - Event->StemBox.Start)/2.0) - (Event->UpperText.Width/2.0);
                        Event->UpperTextBox.End     = Event->UpperTextBox.Start + Event->UpperText.Width;
                    }
                    else
                    {
                        Event->Height += priv->MinEventPad;
                    }

                    // Layout the rest of the boxes for the event.
                    Event->StemBox.Top     = Event->EventBox.Top + Event->Height;
                    Event->StemBox.Bottom  = Event->StemBox.Top + priv->LineWidth;

                    Event->Height += priv->LineWidth;

                    if( Event->LowerText.Str )
                    {
                        sqd_layout_measure_text(sb, &Event->LowerText, EventMaxTextWidth);

                        printf("Pango Event Lower Extents: %g %g\n", Event->LowerText.Width, Event->LowerText.Height); 

                        if( ((2 * priv->TextPad) + Event->UpperText.Height) > priv->MinEventPad )
                            Event->Height += (2 * priv->TextPad) + Event->UpperText.Height;
                        else
                            Event->Height += priv->MinEventPad;

                        Event->LowerTextBox.Top     = Event->StemBox.Bottom + priv->TextPad;  
                        Event->LowerTextBox.Bottom  = Event->LowerTextBox.Top + Event->LowerText.Height;
                        Event->LowerTextBox.Start   = Event->StemBox.Start + ((Event->StemBox.End - Event->StemBox.Start)/2.0) - (Event->LowerText.Width/2.0);
                        Event->LowerTextBox.End     = Event->LowerTextBox.Start + Event->LowerText.Width;
                    }
                    else
                    {
                        Event->Height += priv->MinEventPad;
                    }

                    // Update the EventBox Bottom
                    Event->EventBox.Bottom = Event->EventBox.Top + Event->Height;

                    // The layer height is the maximum height of any event.
                    if( Layer->Height < Event->Height )
                        Layer->Height = Event->Height;

                break;

            }
            
            // Restore default presentation
            sqd_layout_use_default_presentation(sb);

            Element = g_list_next(Element);
        } // Event Layout Loop

        // Get the new event height
        EventTop += Layer->Height;

    } // Event Layer Loop 
}

static void 
sqd_layout_get_event_point( SQDLayout *sb, SQD_OBJ *RefObj, int RefType, double *Top, double *Start )
{
	SQDLayoutPrivate *priv;
    GList           *Element;
    SQD_EVENT_LAYER *Layer;
    SQD_EVENT       *Event;
    int i;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // default the point
    *Top   = 0;
    *Start = 0;

    // Validate the event
    Event = (SQD_EVENT *)RefObj; 

    // Make sure we found it.
    if( (Event == NULL) || (Event->hdr.Type != SDOBJ_EVENT) )
        return;

    switch( RefType )
    {
            // Reference a specific event.
            case NOTE_REFTYPE_EVENT_START: 
                switch ( Event->ArrowDir )
                {
                    case ARROWDIR_STEP:
                        *Top   = Event->StemBox.Top;
                        *Start = Event->StemBox.Start;
                    break;

                    case ARROWDIR_EXTERNAL_TO:
                    case ARROWDIR_LEFT_TO_RIGHT:
                        *Top   = Event->StemBox.Top;
                        *Start = Event->StemBox.Start;
                    break;

                    case ARROWDIR_EXTERNAL_FROM:
                    case ARROWDIR_RIGHT_TO_LEFT:
                        *Top   = Event->StemBox.Top;
                        *Start = Event->StemBox.End;
                    break;
                }
            break;
  
            case NOTE_REFTYPE_EVENT_MIDDLE:  
                if( Event->ArrowDir == ARROWDIR_STEP )
                {
                    // Point to the end of the text in this case, and the center vertically
                    *Top   = (Event->EventBox.Top + Event->EventBox.Bottom)/2.0;
                    *Start = Event->EventBox.End + (2.0*priv->LineWidth);                    
                }
                else
                {
                    // Find the center point of the arrow
                    *Top   = Event->StemBox.Top;
                    *Start = ((Event->StemBox.End - Event->StemBox.Start)/2.0) + Event->StemBox.Start;
                }
            break;

            case NOTE_REFTYPE_EVENT_END: 
                switch ( Event->ArrowDir )
                {
                    case ARROWDIR_STEP:
                        *Top   = Event->StemBox.Bottom;
                        *Start = Event->StemBox.Start;
                    break;

                    case ARROWDIR_EXTERNAL_TO:
                    case ARROWDIR_LEFT_TO_RIGHT:
                        // Get a point at the baseline and stem intersection.
                        *Top   = Event->StemBox.Top;
                        *Start = Event->StemBox.End;
                    break;

                    case ARROWDIR_EXTERNAL_FROM:
                    case ARROWDIR_RIGHT_TO_LEFT:
                        // Get a point at the baseline and stem intersection.
                        *Top   = Event->StemBox.Top;
                        *Start = Event->StemBox.Start;
                    break;
                }
            break;
    } // Ref Type switch


}

static gboolean
sqd_layout_arrange_aregions( SQDLayout *sb )
{
	SQDLayoutPrivate *priv;
    SQD_ACTOR_REGION *AReg;

    int i;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Determine the bounding boxes for each actor region.
    for (i = 0; i < priv->ActorRegions->len; i++)
    {
        AReg = g_ptr_array_index(priv->ActorRegions, i);

        // Setup the parameters
        sqd_layout_use_aregion_presentation(sb, AReg->hdr.ClassStr);

        if( AReg->SEventRef->StemBox.Top >= AReg->EEventRef->StemBox.Bottom )
        {
            g_error("The start event must proceed the end event in an actor region. (failing id '%s'", AReg->hdr.IdStr);
            return TRUE;
        }

        AReg->BoundsBox.Top    = (AReg->SEventRef->StemBox.Top + AReg->SEventRef->StemBox.Bottom)/2.0;
        AReg->BoundsBox.Bottom = (AReg->EEventRef->StemBox.Top + AReg->EEventRef->StemBox.Bottom)/2.0;

        AReg->BoundsBox.Start  = AReg->ActorRef->StemBox.Start + (priv->LineWidth/2.0) - (2.0*priv->LineWidth);
        AReg->BoundsBox.End    = AReg->ActorRef->StemBox.Start + (priv->LineWidth/2.0) + (2.0*priv->LineWidth);

        debug_box_print("ARegion Box", &AReg->BoundsBox);

        // Restore default presentation
        sqd_layout_use_default_presentation(sb);

    }

    return FALSE;
}

static void 
sqd_layout_get_aregion_point( SQDLayout *sb, SQD_OBJ *RefObj, double *Top, double *Start )
{
	SQDLayoutPrivate *priv;
    SQD_ACTOR_REGION *AReg;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // default the point
    *Top   = 0;
    *Start = 0;

    // Look up the actor element
    AReg = (SQD_ACTOR_REGION *)RefObj;

    if( (AReg == NULL) || (AReg->hdr.Type != SDOBJ_AREGION) )
        return;

    // Get a point at the baseline and stem intersection.
    *Top   = AReg->BoundsBox.Top + (5.0*priv->LineWidth);
    *Start = AReg->BoundsBox.End;
}

static gboolean
sqd_layout_arrange_bregions( SQDLayout *sb )
{
	SQDLayoutPrivate *priv;
    SQD_BOX_REGION   *BReg;
    int i;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Determine the bounding boxes for each actor region.
    for (i = 0; i < priv->BoxRegions->len; i++)
    {
        BReg = g_ptr_array_index(priv->BoxRegions, i);

        // Setup the parameters
        sqd_layout_use_bregion_presentation(sb, BReg->hdr.ClassStr);

        if( BReg->SEventRef->EventBox.Top >= BReg->EEventRef->EventBox.Bottom )
        {
            g_error("The start event must proceed the end event in a box region. (failing id '%s'", BReg->hdr.IdStr);
            return TRUE;
        }

        BReg->BoundsBox.Top    = BReg->SEventRef->EventBox.Top;
        BReg->BoundsBox.Bottom = BReg->EEventRef->EventBox.Bottom;

        if( BReg->SActorRef->BoundsBox.Top >= BReg->EActorRef->BoundsBox.Bottom )
        {
            g_error("The start actor must be to the right of the end actor in a box region. (failing id '%s'", BReg->hdr.IdStr);
            return TRUE;
        }

        BReg->BoundsBox.Start  = BReg->SActorRef->BoundsBox.Start;
        BReg->BoundsBox.End    = BReg->EActorRef->BoundsBox.End;

        debug_box_print("BRegion Box", &BReg->BoundsBox);

        // Restore default presentation
        sqd_layout_use_default_presentation(sb);

    }

    return 0;
}

static void 
sqd_layout_get_bregion_point( SQDLayout *sb, SQD_OBJ *RefObj, double *Top, double *Start )
{
	SQDLayoutPrivate *priv;
    SQD_BOX_REGION   *BReg;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // default the point
    *Top   = 0;
    *Start = 0;

    // Look up the actor element
    BReg = (SQD_BOX_REGION *)RefObj;

    if( (BReg == NULL) || (BReg->hdr.Type != SDOBJ_BREGION) )
        return;

    // Get a point at the baseline and stem intersection.
    *Top   = BReg->BoundsBox.Top + (3.0 * priv->LineWidth);
    *Start = BReg->BoundsBox.End - (3.0 * priv->LineWidth);
}

static double
sqd_layout_arrange_notes_references( SQDLayout *sb )
{
	SQDLayoutPrivate *priv;
    SQD_NOTE *Note;
    int i;
    double NoteTop;
    double NoteTextWidth;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // The Note Box should now contain the space allocated for Note columns. 
    NoteTop   = priv->NoteBox.Top + priv->ElementPad;
    NoteTextWidth = priv->NoteBoxWidth - (2 * priv->TextPad);

    // Determine the height of the Note text.
    for (i = 0; i <= priv->MaxNoteIndex; i++)
    {
        Note = g_ptr_array_index(priv->Notes, i);

        // Setup the parameters
        sqd_layout_use_noteref_presentation(sb, Note->hdr.ClassStr);

        // Start the arrow at the note.
        Note->RefFirstTop   = Note->BoundsBox.Top;
        Note->RefFirstStart = Note->BoundsBox.Start;

        switch( Note->ReferenceType )
        {
            // Just a general note that doesn't reference a specific diagram feature.
            case NOTE_REFTYPE_NONE:          
            break;

            // References the a specific Actor.
            case NOTE_REFTYPE_ACTOR: 
                sqd_layout_get_actor_point( sb, Note->RefObj, &Note->RefLastTop, &Note->RefLastStart );
            break;

            // Reference a specific event.
            case NOTE_REFTYPE_EVENT_START:   
            case NOTE_REFTYPE_EVENT_MIDDLE:  
            case NOTE_REFTYPE_EVENT_END:     
                sqd_layout_get_event_point( sb, Note->RefObj, Note->ReferenceType, &Note->RefLastTop, &Note->RefLastStart );
            break;
           
            // Reference to a Vertical Span of events.
            case NOTE_REFTYPE_VSPAN:         
                sqd_layout_get_aregion_point( sb, Note->RefObj, &Note->RefLastTop, &Note->RefLastStart );
            break;

            // Group events into a box. Reference to the box.
            case NOTE_REFTYPE_BOXSPAN:       
                sqd_layout_get_bregion_point( sb, Note->RefObj, &Note->RefLastTop, &Note->RefLastStart );
            break;
        } // Ref Type switch

        // Restore default presentation
        sqd_layout_use_default_presentation(sb);

    } // Note Loop
}

static int
sqd_layout_arrange_diagram( SQDLayout *sb )
{
	SQDLayoutPrivate *priv;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Start as if there isn't a title.
    priv->TitleBox.Start   = priv->Margin;
    priv->TitleBox.End     = priv->Width - priv->Margin;
    priv->TitleBox.Top     = priv->Margin;
    priv->TitleBox.Bottom  = priv->Margin;

    // Determine the amount of space needed for the title
    if( priv->Title.Str )
    {
        // Setup the parameters for the title bar
        sqd_layout_use_title_presentation(sb);

        priv->TitleBar.Start   = priv->TitleBox.Start;
        priv->TitleBar.End     = priv->TitleBox.End;
        priv->TitleBar.Top     = priv->TitleBox.Top + priv->ElementPad;

        sqd_layout_measure_text(sb, &priv->Title, (priv->TitleBar.End - priv->TitleBar.Start - (2 * priv->TextPad)));


        priv->TitleBar.Bottom  = priv->TitleBar.Top + priv->Title.Height + (2 * priv->TextPad);
        priv->TitleBox.Bottom  = priv->TitleBar.Bottom + priv->ElementPad;

        printf("Pango Title Extents: %g %g\n", priv->Title.Width, priv->Title.Height); 
        debug_box_print("TitleBar", &priv->TitleBar);
        debug_box_print("TitleBox", &priv->TitleBox);

        // Restore defaults
        sqd_layout_use_default_presentation(sb);

    }

    // Default to not having a Description
    priv->DescriptionBox.Start   = priv->Margin;
    priv->DescriptionBox.End     = priv->Width - priv->Margin;
    priv->DescriptionBox.Top     = priv->TitleBox.Bottom;
    priv->DescriptionBox.Bottom  = priv->TitleBox.Bottom;

    // Determine the amount of space needed for the description block
    if( priv->Description.Str )
    {
        // Setup the parameters for the title bar
        sqd_layout_use_description_presentation(sb);

        sqd_layout_measure_text(sb, &priv->Description, (priv->DescriptionBox.End - priv->DescriptionBox.Start - (2 * priv->TextPad)));

        priv->DescriptionBox.Bottom  = priv->TitleBox.Bottom + priv->Description.Height + (2 * priv->ElementPad) + (2 * priv->TextPad);

        printf("Pango Description Extents: %g %g\n", priv->Description.Width,priv->Description.Height);  
        debug_box_print("DescriptionBox", &priv->DescriptionBox);

        // Restore default presentation
        sqd_layout_use_default_presentation(sb);
    }

    // Determine if a notes column is needed.
    if( priv->Notes->len )
    {
        // Add a box for notes on the right hand side of the page.
        priv->NoteBox.End    = priv->Width - priv->Margin;
        priv->NoteBox.Start  = priv->NoteBox.End - priv->NoteBoxWidth;

        // Figure out where the Actors should be located.
        priv->ActorBox.Start  = priv->Margin;
        priv->ActorBox.End    = priv->NoteBox.Start - priv->ElementPad;
        priv->ActorBox.Top    = priv->DescriptionBox.Bottom;
        priv->ActorBox.Bottom = priv->Height - priv->Margin;

        // Call a subroutine to layout the Actors.
        priv->SeqBox.Top = sqd_layout_arrange_actors(sb);

        // Finish laying out the notes
        priv->NoteBox.Top    = priv->SeqBox.Top;
        priv->NoteBox.Bottom = priv->Height - priv->Margin;

        sqd_layout_arrange_notes(sb);

        // Init the rest of the event box.
        priv->SeqBox.Start  = priv->ActorBox.Start;
        priv->SeqBox.End    = priv->ActorBox.End;
        priv->SeqBox.Bottom = priv->ActorBox.Bottom;

        // Layout the events
        sqd_layout_arrange_events(sb);

        // Layout the actor regions
        sqd_layout_arrange_aregions(sb);

        // Layout the box regions
        sqd_layout_arrange_bregions(sb);

        // Layout the reference lines for notes.
        sqd_layout_arrange_notes_references(sb);
    }
    else
    {
        // Figure out where the Actors should be located.
        // In the future the width will depend on if notes are present or not.
        priv->ActorBox.Start  = priv->Margin;
        priv->ActorBox.End    = priv->Width - priv->Margin;
        priv->ActorBox.Top    = priv->DescriptionBox.Bottom;
        priv->ActorBox.Bottom = priv->Height - priv->Margin;

        // Call a subroutine to layout the Actors.
        priv->SeqBox.Top = sqd_layout_arrange_actors(sb);

        // Init the rest of the event box.
        // In the future the width will depend on if notes are present or not.
        priv->SeqBox.Start  = priv->Margin;
        priv->SeqBox.End    = priv->Width - priv->Margin;
        priv->SeqBox.Bottom = priv->ActorBox.Bottom;

        // Layout the events
        sqd_layout_arrange_events(sb);

        // Layout the actor regions
        sqd_layout_arrange_aregions(sb);

        // Layout the box regions
        sqd_layout_arrange_bregions(sb);
    }

}

static void
sqd_layout_draw_text( SQDLayout *sb, SQD_TXT *Text, double Width )
{
	SQDLayoutPrivate *priv;
    PangoLayout *layout;
    PangoFontDescription *desc;
    int pwidth, pheight;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Create a PangoLayout, set the font and text 
    layout = pango_cairo_create_layout (priv->cr);
  
    if( Width )
    {
        pango_layout_set_width (layout, (Width * PANGO_SCALE));
        pango_layout_set_wrap (layout, PANGO_WRAP_WORD);
    }

    desc = pango_font_description_from_string( priv->FontStr );
    pango_layout_set_font_description (layout, desc);
    pango_font_description_free (desc);

    pango_layout_set_markup (layout, Text->Str, -1);

    pango_cairo_show_layout (priv->cr, layout);

    // free the layout object 
    g_object_unref (layout);
}

static void
sqd_layout_draw_actors( SQDLayout *sb )
{
	SQDLayoutPrivate *priv;
    SQD_ACTOR *Actor;
    int i;
    double ActorWidth;
    double ActorTextWidth;
    double dashes[] = {1.0,  /* ink */
                       2.0,  /* skip */
                      };
    int    ndash  = sizeof (dashes)/sizeof(dashes[0]);
    double offset = 2.0;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    ActorWidth = (priv->ActorBox.End - priv->ActorBox.Start) / (priv->MaxActorIndex + 1.0);
    ActorTextWidth = (ActorWidth *2.0)/3.0;

    // Draw each actor
    for (i = 0; i <= priv->MaxActorIndex; i++)
    {
        Actor = g_ptr_array_index(priv->Actors, i);

        // Setup the actor presentation parameters
        sqd_layout_use_actor_presentation(sb, Actor->hdr.ClassStr);

        // Draw the Text bounding box.
        cairo_set_source_rgba(priv->cr, priv->FillColor.Red, priv->FillColor.Green, priv->FillColor.Blue, priv->FillColor.Alpha);

        cairo_rectangle(priv->cr, Actor->NameBox.Start, Actor->NameBox.Top, 
                            (Actor->NameBox.End - Actor->NameBox.Start),
                            (Actor->NameBox.Bottom - Actor->NameBox.Top));

        cairo_fill (priv->cr);


        // Draw the Actor Title
        // Center it over the Stem
        cairo_set_source_rgba(priv->cr, priv->TextColor.Red, priv->TextColor.Green, priv->TextColor.Blue, priv->TextColor.Alpha);

        cairo_move_to (priv->cr, 
                       ((Actor->StemBox.Start + priv->LineWidth) - (Actor->Name.Width / 2.0)),
                       (Actor->NameBox.Top + priv->TextPad));

        sqd_layout_draw_text( sb, &Actor->Name, ActorTextWidth );

    
        // Draw the Baseline
        cairo_set_source_rgba(priv->cr, priv->LineColor.Red, priv->LineColor.Green, priv->LineColor.Blue, priv->LineColor.Alpha);

        cairo_move_to (priv->cr, Actor->BaselineBox.Start, Actor->BaselineBox.Top + (priv->LineWidth/2.0));
        cairo_line_to (priv->cr, Actor->BaselineBox.End, Actor->BaselineBox.Top + (priv->LineWidth/2.0));

        cairo_stroke (priv->cr);


        // Draw the Stem
        cairo_set_source_rgba(priv->cr, priv->StemColor.Red, priv->StemColor.Green, priv->StemColor.Blue, priv->StemColor.Alpha);
        //cairo_set_dash (priv->cr, dashes, ndash, offset);

        cairo_move_to (priv->cr, Actor->StemBox.Start + (priv->LineWidth/2.0), Actor->StemBox.Top);
        cairo_line_to (priv->cr, Actor->StemBox.Start + (priv->LineWidth/2.0), Actor->StemBox.Bottom);

        cairo_stroke (priv->cr);

        // Back to default rendering settings
        sqd_layout_use_default_presentation(sb);
    }

}

static void
sqd_layout_draw_events( SQDLayout *sb )
{
	SQDLayoutPrivate *priv;
    GList            *Element;
    SQD_EVENT_LAYER  *Layer;
    SQD_EVENT        *Event;
    int i;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Cycle through the event layers in sequencial order to layout each one.
    for (i = 0; i <= priv->MaxEventIndex; i++)
    {
        Layer = &g_array_index(priv->EventLayers, SQD_EVENT_LAYER, i);

        // Layout each seperate event in this layer
        Element = g_list_first(Layer->Events);
        while( Element )
        {
            Event = Element->data;

            // Setup the parameters
            sqd_layout_use_event_presentation(sb, Event->hdr.ClassStr);

            // Calculate the arrow length so that available space for text layout can be calculated.
            switch ( Event->ArrowDir )
            {
                case ARROWDIR_EXTERNAL_TO:

                    cairo_set_source_rgba( priv->cr, priv->StemColor.Red, priv->StemColor.Green, priv->StemColor.Blue, priv->StemColor.Alpha);

                    // Draw the Stem
                    cairo_move_to (priv->cr, Event->StemBox.Start, Event->StemBox.Top + (priv->LineWidth/2.0));
                    cairo_line_to (priv->cr, Event->StemBox.End, Event->StemBox.Top + (priv->LineWidth/2.0));

                    cairo_stroke (priv->cr);

                    cairo_move_to (priv->cr, Event->StemBox.End - priv->ArrowLength, Event->StemBox.Top + (priv->LineWidth/2.0) - (priv->ArrowWidth/2.0));
                    cairo_line_to (priv->cr, Event->StemBox.End, Event->StemBox.Top + (priv->LineWidth/2.0));
                    cairo_line_to (priv->cr, Event->StemBox.End - priv->ArrowLength, Event->StemBox.Top + (priv->LineWidth/2.0) + (priv->ArrowWidth/2.0));

                    cairo_stroke (priv->cr);
                break;

                case ARROWDIR_EXTERNAL_FROM:

                    cairo_set_source_rgba( priv->cr, priv->StemColor.Red, priv->StemColor.Green, priv->StemColor.Blue, priv->StemColor.Alpha);

                    // Draw the Stem
                    cairo_move_to (priv->cr, Event->StemBox.Start, Event->StemBox.Top + (priv->LineWidth/2.0));
                    cairo_line_to (priv->cr, Event->StemBox.End, Event->StemBox.Top + (priv->LineWidth/2.0));

                    cairo_stroke (priv->cr);

                    cairo_move_to (priv->cr, Event->StemBox.Start + priv->ArrowLength, Event->StemBox.Top + (priv->LineWidth/2.0) - (priv->ArrowWidth/2.0));
                    cairo_line_to (priv->cr, Event->StemBox.Start, Event->StemBox.Top + (priv->LineWidth/2.0));
                    cairo_line_to (priv->cr, Event->StemBox.Start + priv->ArrowLength, Event->StemBox.Top + (priv->LineWidth/2.0) + (priv->ArrowWidth/2.0));

                    cairo_stroke (priv->cr);
                break;

                case ARROWDIR_STEP:

                    cairo_set_source_rgba( priv->cr, priv->StemColor.Red, priv->StemColor.Green, priv->StemColor.Blue, priv->StemColor.Alpha);
                  
                    // Draw the Stem
                    cairo_move_to (priv->cr, Event->StemBox.Start, Event->StemBox.Top + (priv->LineWidth/2.0));
                    cairo_line_to(priv->cr, (Event->StemBox.Start + Event->StemBox.End)/2.0, Event->StemBox.Top + (priv->LineWidth/2.0));
                    cairo_curve_to(priv->cr, Event->StemBox.End, Event->StemBox.Top + (priv->LineWidth/2.0), 
                                             Event->StemBox.End, Event->StemBox.Bottom - (priv->LineWidth/2.0),
                                             (Event->StemBox.Start + Event->StemBox.End)/2.0, Event->StemBox.Bottom - (priv->LineWidth/2.0));
                    cairo_line_to(priv->cr, Event->StemBox.Start, Event->StemBox.Bottom - (priv->LineWidth/2.0));

                    cairo_stroke (priv->cr);

                    cairo_move_to (priv->cr, Event->StemBox.Start + priv->ArrowLength, Event->StemBox.Bottom - (priv->LineWidth/2.0) - (priv->ArrowWidth/2.0));
                    cairo_line_to (priv->cr, Event->StemBox.Start, Event->StemBox.Bottom - (priv->LineWidth/2.0));
                    cairo_line_to (priv->cr, Event->StemBox.Start + priv->ArrowLength, Event->StemBox.Bottom - (priv->LineWidth/2.0) + (priv->ArrowWidth/2.0));

                    cairo_stroke (priv->cr);
                break;

                case ARROWDIR_LEFT_TO_RIGHT:

                    cairo_set_source_rgba( priv->cr, priv->StemColor.Red, priv->StemColor.Green, priv->StemColor.Blue, priv->StemColor.Alpha);

                    // Draw the Stem
                    cairo_move_to (priv->cr, Event->StemBox.Start, Event->StemBox.Top + (priv->LineWidth/2.0));
                    cairo_line_to (priv->cr, Event->StemBox.End, Event->StemBox.Top + (priv->LineWidth/2.0));

                    cairo_stroke (priv->cr);

                    cairo_move_to (priv->cr, Event->StemBox.End - priv->ArrowLength, Event->StemBox.Top + (priv->LineWidth/2.0) - (priv->ArrowWidth/2.0));
                    cairo_line_to (priv->cr, Event->StemBox.End, Event->StemBox.Top + (priv->LineWidth/2.0));
                    cairo_line_to (priv->cr, Event->StemBox.End - priv->ArrowLength, Event->StemBox.Top + (priv->LineWidth/2.0) + (priv->ArrowWidth/2.0));

                    cairo_stroke (priv->cr);

                break;

                case ARROWDIR_RIGHT_TO_LEFT:

                    cairo_set_source_rgba( priv->cr, priv->StemColor.Red, priv->StemColor.Green, priv->StemColor.Blue, priv->StemColor.Alpha);

                    // Draw the Stem
                    cairo_move_to (priv->cr, Event->StemBox.Start, Event->StemBox.Top + (priv->LineWidth/2.0));
                    cairo_line_to (priv->cr, Event->StemBox.End, Event->StemBox.Top + (priv->LineWidth/2.0));

                    cairo_stroke (priv->cr);

                    cairo_move_to (priv->cr, Event->StemBox.Start + priv->ArrowLength, Event->StemBox.Top + (priv->LineWidth/2.0) - (priv->ArrowWidth/2.0));
                    cairo_line_to (priv->cr, Event->StemBox.Start, Event->StemBox.Top + (priv->LineWidth/2.0));
                    cairo_line_to (priv->cr, Event->StemBox.Start + priv->ArrowLength, Event->StemBox.Top + (priv->LineWidth/2.0) + (priv->ArrowWidth/2.0));

                    cairo_stroke (priv->cr);

                break;

            }
 
            cairo_set_source_rgba( priv->cr, priv->TextColor.Red, priv->TextColor.Green, priv->TextColor.Blue, priv->TextColor.Alpha);

            if( Event->UpperText.Str )
            {
                cairo_move_to (priv->cr, Event->UpperTextBox.Start, Event->UpperTextBox.Top);

                sqd_layout_draw_text( sb, &Event->UpperText, Event->UpperText.Width );
            }
    
            if( Event->LowerText.Str )
            {
                cairo_move_to (priv->cr, Event->LowerTextBox.Start, Event->LowerTextBox.Top);

                sqd_layout_draw_text( sb, &Event->LowerText, Event->LowerText.Width );
            }

            // Switch back to the default presentation
            sqd_layout_use_default_presentation(sb);
           
            Element = g_list_next(Element);
        } // Event Layout Loop
    } // Event Layer Loop 

}

static void
sqd_layout_draw_aregions( SQDLayout *sb )
{
	SQDLayoutPrivate *priv;
    SQD_ACTOR_REGION *AReg;
    int i;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Draw each actor region box
    for (i = 0; i <= priv->ActorRegions->len; i++)
    {
        AReg = g_ptr_array_index(priv->ActorRegions, i);

        // Set the presentation
        sqd_layout_use_aregion_presentation(sb, AReg->hdr.ClassStr);

        // Draw the Text bounding box.
        cairo_set_source_rgba( priv->cr, priv->FillColor.Red, priv->FillColor.Green, priv->FillColor.Blue, priv->FillColor.Alpha );

//        sqd_layout_draw_rounded_rec(sb, AReg->BoundsBox.Start, AReg->BoundsBox.Top, 
//                            (AReg->BoundsBox.End - AReg->BoundsBox.Start),
//                            (AReg->BoundsBox.Bottom - AReg->BoundsBox.Top), 10);

        cairo_rectangle(priv->cr, AReg->BoundsBox.Start, AReg->BoundsBox.Top, 
                            (AReg->BoundsBox.End - AReg->BoundsBox.Start),
                            (AReg->BoundsBox.Bottom - AReg->BoundsBox.Top));

        cairo_fill (priv->cr);

        // Back to the defualt presentation
        sqd_layout_use_default_presentation(sb);
    }

}

static void
sqd_layout_draw_bregions( SQDLayout *sb )
{
	SQDLayoutPrivate *priv;
    SQD_BOX_REGION   *BReg;
    int i;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Draw each box region box
    for (i = 0; i <= priv->BoxRegions->len; i++)
    {
        BReg = g_ptr_array_index(priv->BoxRegions, i);

        // Set the presentation
        sqd_layout_use_bregion_presentation(sb, BReg->hdr.ClassStr);

        // Draw the Text bounding box.
        cairo_set_source_rgba( priv->cr, priv->FillColor.Red, priv->FillColor.Green, priv->FillColor.Blue, priv->FillColor.Alpha );

        sqd_layout_draw_rounded_rec(sb, BReg->BoundsBox.Start, BReg->BoundsBox.Top, 
                            (BReg->BoundsBox.End - BReg->BoundsBox.Start),
                            (BReg->BoundsBox.Bottom - BReg->BoundsBox.Top), 10);

        //cairo_rectangle(priv->cr, BReg->BoundsBox.Start, BReg->BoundsBox.Top, 
        //                    (BReg->BoundsBox.End - BReg->BoundsBox.Start),
        //                    (BReg->BoundsBox.Bottom - BReg->BoundsBox.Top));

        cairo_fill (priv->cr);

        // Back to the defualt presentation
        sqd_layout_use_default_presentation(sb);
    }

}


static void
sqd_layout_draw_notes( SQDLayout *sb )
{
	SQDLayoutPrivate *priv;
    SQD_NOTE *Note;
    int i;
    double NoteTextWidth;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    NoteTextWidth = priv->NoteBoxWidth - (2 * priv->TextPad);

    // Draw each actor
    for (i = 0; i <= priv->MaxNoteIndex; i++)
    {
        Note = g_ptr_array_index(priv->Notes, i);

        // Setup the parameters
        sqd_layout_use_note_presentation(sb, Note->hdr.ClassStr);

        // Draw the Text bounding box.
        cairo_set_source_rgba( priv->cr, priv->FillColor.Red, priv->FillColor.Green, priv->FillColor.Blue, priv->FillColor.Alpha);

        cairo_rectangle(priv->cr, Note->BoundsBox.Start, Note->BoundsBox.Top, 
                            (Note->BoundsBox.End - Note->BoundsBox.Start),
                            (Note->BoundsBox.Bottom - Note->BoundsBox.Top));

        cairo_fill (priv->cr);


        // Draw the Note Text
        cairo_set_source_rgba( priv->cr, priv->TextColor.Red, priv->TextColor.Green, priv->TextColor.Blue, priv->TextColor.Alpha);

        cairo_move_to (priv->cr, (Note->BoundsBox.Start + priv->TextPad), (Note->BoundsBox.Top + priv->TextPad));

        sqd_layout_draw_text( sb, &Note->Text, NoteTextWidth );

        // Back to the default parameters
        sqd_layout_use_default_presentation(sb);     
    }

}



static void
sqd_layout_draw_note_references( SQDLayout *sb )
{
	SQDLayoutPrivate *priv;
    SQD_NOTE *Note;
    int i;
    double dashes[] = {3.0,  /* ink */
                       4.0,  /* skip */
                       1.0,  /* ink */
                       4.0   /* skip*/
                      };
    int    ndash  = sizeof (dashes)/sizeof(dashes[0]);
    double offset = -50.0;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Draw each reference line
    for (i = 0; i <= priv->MaxNoteIndex; i++)
    {
        Note = g_ptr_array_index(priv->Notes, i);

        // If this note doesn't have a reference then don't draw one.
        if( Note->ReferenceType == NOTE_REFTYPE_NONE )
            continue;

        // Setup the parameters
        sqd_layout_use_noteref_presentation(sb, Note->hdr.ClassStr);

        // Setup to draw the reference line.
        cairo_set_source_rgba (priv->cr, priv->StemColor.Red, priv->StemColor.Green, priv->StemColor.Blue, priv->StemColor.Alpha);
        cairo_set_line_cap  (priv->cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_dash (priv->cr, dashes, ndash, offset);

        cairo_move_to (priv->cr, Note->BoundsBox.Start, Note->BoundsBox.Top);
        cairo_line_to (priv->cr, Note->RefLastStart, Note->RefLastTop ); 
        cairo_stroke (priv->cr);

        // Draw a small circle at the termination of the note reference.
        cairo_arc(priv->cr, Note->RefLastStart, Note->RefLastTop, 2*priv->LineWidth, 0.0, 2*M_PI );
        cairo_fill(priv->cr);

        // Back to the default parameters
        sqd_layout_use_default_presentation(sb);     
    }

}

static int
sqd_layout_draw_diagram( SQDLayout *sb )
{
	SQDLayoutPrivate *priv;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    cairo_set_line_width (priv->cr, priv->LineWidth);
    cairo_set_source_rgb (priv->cr, 0, 0, 0);

    if( priv->Title.Str )
    {
        // Setup the parameters for the title bar
        sqd_layout_use_title_presentation(sb);

        priv->TitleBar.Start   = priv->TitleBox.Start;

        cairo_set_source_rgba(priv->cr, priv->FillColor.Red, priv->FillColor.Green, priv->FillColor.Blue, priv->FillColor.Alpha);

        cairo_rectangle(priv->cr, priv->TitleBar.Start, priv->TitleBar.Top, 
                            (priv->TitleBar.End - priv->TitleBar.Start),
                            (priv->TitleBar.Bottom - priv->TitleBar.Top));

        cairo_fill (priv->cr);

        cairo_set_source_rgba(priv->cr, priv->TextColor.Red, priv->TextColor.Green, priv->TextColor.Blue, priv->TextColor.Alpha);

        cairo_move_to (priv->cr, (priv->TitleBar.Start + priv->TextPad), (priv->TitleBar.Top + priv->TextPad));

        sqd_layout_draw_text( sb, &priv->Title, (priv->TitleBar.End - priv->TitleBar.Start) );

        // Back to the default presentation
        sqd_layout_use_default_presentation(sb);
    }

    // Determine the amount of space needed for the description block
    if( priv->Description.Str )
    {
        // Setup the parameters for the description region
        sqd_layout_use_description_presentation(sb);

        cairo_set_source_rgba(priv->cr, priv->TextColor.Red, priv->TextColor.Green, priv->TextColor.Blue, priv->TextColor.Alpha);

        cairo_move_to (priv->cr, 
                        (priv->DescriptionBox.Start + priv->TextPad), 
                        (priv->DescriptionBox.Top + priv->ElementPad + priv->TextPad));

        sqd_layout_draw_text( sb, &priv->Description, (priv->DescriptionBox.End - priv->DescriptionBox.Start) );

        // Back to the default presentation
        sqd_layout_use_default_presentation(sb);

    }

    sqd_layout_draw_actors(sb);

    sqd_layout_draw_events(sb);

    sqd_layout_draw_notes(sb);

    sqd_layout_draw_aregions(sb);

    sqd_layout_draw_bregions(sb);

    sqd_layout_draw_note_references(sb);

}

///////////////////////
// Interface functions
///////////////////////
SQDLayout*
sqd_layout_new (void)
{
	return g_object_new (G_TYPE_SQD_LAYOUT, NULL);
}

// Set the base URL for the Hudson instance.
gboolean
sqd_layout_set_name( SQDLayout *sb, gchar *NameStr )
{
	SQDLayoutPrivate *priv;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    if(priv->Title.Str)
        g_free(priv->Title.Str);

    priv->Title.Str = g_strdup(NameStr);

    return FALSE;
}

gboolean
sqd_layout_set_description( SQDLayout *sb, gchar *DescStr )
{
	SQDLayoutPrivate *priv;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    if(priv->Description.Str)
        g_free(priv->Description.Str);

    priv->Description.Str = g_strdup(DescStr);

    return FALSE;
}

static gboolean
sqd_layout_add_event_common( SQDLayout *sb, SQD_EVENT *Event)
{
	SQDLayoutPrivate *priv;
    SQD_EVENT_LAYER  *Layer;
    guint32          TmpMask, i;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    printf("Event Common: %d, %s\n", Event->hdr.Index, Event->hdr.IdStr);

    // Add this object to the ID hash table.
    g_hash_table_insert( priv->IdTable, Event->hdr.IdStr, Event );

    if( priv->EventLayers->len < (Event->hdr.Index + 1) )
    {
        g_array_set_size(priv->EventLayers, (Event->hdr.Index + 1));
    }
    Layer = &g_array_index(priv->EventLayers, SQD_EVENT_LAYER, Event->hdr.Index);

    Layer->EventCnt += 1;

    switch ( Event->ArrowDir )
    {
        // External events need to be in a layer by themselves.
        case ARROWDIR_EXTERNAL_TO:
            if( Layer->EventCnt > 1 )
            {
                g_error("External events must be in there own slot.");
                return TRUE;
            }
            Layer->ExternalLayer = TRUE;
        break;

        // External events need to be in a layer by themselves.
        case ARROWDIR_EXTERNAL_FROM:
            if( Layer->EventCnt > 1 )
            {
                g_error("External events must be in there own slot.");
                return TRUE;
            }
            Layer->ExternalLayer = TRUE;
        break;

        case ARROWDIR_STEP:
            if( (Layer->EventCnt > 1) && (Layer->StepLayer == FALSE) )
            {
                g_error("Step events can only share a slot with other step events.");
                return TRUE;
            }
            Layer->StepLayer = TRUE;
            TmpMask = 0x1 << (Event->StartActorIndx + 1);
        break;

        case ARROWDIR_LEFT_TO_RIGHT:
            if( (Layer->EventCnt > 1) && (Layer->RegularLayer == FALSE) )
            {
                g_error("Regular events can only share a slot with other regular events.");
                return TRUE;
            }
            Layer->RegularLayer = TRUE;

            TmpMask = 0;
            for(i = Event->StartActorIndx; i <= Event->EndActorIndx; i++)
                TmpMask |= 0x1 << (i + 1);
        break;

        case ARROWDIR_RIGHT_TO_LEFT:
            if( (Layer->EventCnt > 1) && (Layer->RegularLayer == FALSE) )
            {
                g_error("Regular events can only share a slot with other regular events.");
                return TRUE;
            }
            Layer->RegularLayer = TRUE;
            TmpMask = 0;
            for(i = Event->EndActorIndx; i <= Event->StartActorIndx; i++)
                TmpMask |= 0x1 << (i + 1);
        break;
    }

    printf("Event Layer Masks: %d, 0x%x 0x%x\n", Event->hdr.Index, Layer->UsedMask, TmpMask);

    if( Layer->UsedMask & TmpMask )
    {
        printf("ERROR: Event Collision\n");
        exit(-1);
    }

    Layer->UsedMask |= TmpMask;

    printf("0x%x\n", Layer->Events);

    Layer->Events = g_list_append(Layer->Events, Event);

}

gboolean
sqd_layout_add_event( SQDLayout *sb, gchar *IdStr, gchar *ClassStr, int SlotIndex, gchar *StartActorId, gchar *EndActorId, char *TopLabel, char *BottomLabel)
{
	SQDLayoutPrivate *priv;
    SQD_EVENT        *TmpEvent;
    SQD_EVENT_LAYER  *Layer;
    guint32          TmpMask, i;
    SQD_ACTOR        *SAPtr;
    SQD_ACTOR        *EAPtr;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Make sure the ID isn't already in use
    if( g_hash_table_lookup(priv->IdTable, IdStr) != NULL )
    {
        g_error("Sequence object id \"%s\" already exists. Ids must be unique.\n", IdStr);
        return TRUE;
    }

    // Lookup the start actor
    SAPtr = g_hash_table_lookup(priv->IdTable, StartActorId);
    g_print("Start Actor Lookup: 0x%x, %d, %d, %s\n", SAPtr, SAPtr->hdr.Index, SAPtr->hdr.Type, SAPtr->hdr.IdStr); 
    if( SAPtr == NULL )
    {
        g_error("Couldn't find start actor with id \"%s\".\n", StartActorId);
        return TRUE;
    }
    if( SAPtr->hdr.Type != SDOBJ_ACTOR )
    {
        g_error("Object with id \"%s\" is not of the required actor type.\n", StartActorId);
        return TRUE;
    }

    // Lookup the end actor
    EAPtr = g_hash_table_lookup(priv->IdTable, EndActorId);
    g_print("End Actor Lookup: 0x%x, %d, %d, %s\n", EAPtr, EAPtr->hdr.Index, EAPtr->hdr.Type, EAPtr->hdr.IdStr); 
    if( EAPtr == NULL )
    {
        g_error("Couldn't find end actor with id \"%s\".\n", EndActorId);
        return TRUE;
    }
    if( EAPtr->hdr.Type != SDOBJ_ACTOR )
    {
        g_error("Object with id \"%s\" is not of the required actor type.\n", EndActorId);
        return TRUE;
    }

    TmpEvent = malloc( sizeof(SQD_EVENT) );

    memset(TmpEvent, 0, sizeof(SQD_EVENT));

    TmpEvent->hdr.Index         = SlotIndex;
    TmpEvent->hdr.Type          = SDOBJ_EVENT;
    TmpEvent->hdr.IdStr         = g_strdup(IdStr);
    TmpEvent->hdr.ClassStr      = ClassStr ? g_strdup(ClassStr):NULL;

    if( TmpEvent->hdr.Index > priv->MaxEventIndex )
        priv->MaxEventIndex = TmpEvent->hdr.Index;

    TmpEvent->StartActorIndx    = SAPtr->hdr.Index;
    TmpEvent->EndActorIndx      = EAPtr->hdr.Index;

    if( TmpEvent->StartActorIndx < TmpEvent->EndActorIndx )
        TmpEvent->ArrowDir = ARROWDIR_LEFT_TO_RIGHT;
    else 
        TmpEvent->ArrowDir = ARROWDIR_RIGHT_TO_LEFT;

    if( TopLabel )
        TmpEvent->UpperText.Str  = strdup(TopLabel);

    if( BottomLabel )
        TmpEvent->LowerText.Str  = strdup(BottomLabel);

    sqd_layout_add_event_common( sb, TmpEvent);

}

gboolean
sqd_layout_add_step_event( SQDLayout *sb, gchar *IdStr, gchar *ClassStr, int SlotIndex, gchar *ActorId, gchar *Label)
{
	SQDLayoutPrivate *priv;
    SQD_EVENT        *TmpEvent;
    SQD_EVENT_LAYER  *Layer;
    guint32          TmpMask, i;
    SQD_ACTOR        *SAPtr;
    SQD_ACTOR        *EAPtr;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Make sure the ID isn't already in use
    if( g_hash_table_lookup(priv->IdTable, IdStr) != NULL )
    {
        g_error("Sequence object id \"%s\" already exists. Ids must be unique.\n", IdStr);
        return TRUE;
    }

    // Lookup the actor
    SAPtr = g_hash_table_lookup(priv->IdTable, ActorId);
    g_print("Actor Lookup: 0x%x, %d, %d, %s\n", SAPtr, SAPtr->hdr.Index, SAPtr->hdr.Type, SAPtr->hdr.IdStr); 
    if( SAPtr == NULL )
    {
        g_error("Couldn't find start actor with id \"%s\".\n", ActorId);
        return TRUE;
    }
    if( SAPtr->hdr.Type != SDOBJ_ACTOR )
    {
        g_error("Object with id \"%s\" is not of the required actor type.\n", ActorId);
        return TRUE;
    }

    TmpEvent = malloc( sizeof(SQD_EVENT) );

    memset(TmpEvent, 0, sizeof(SQD_EVENT));

    TmpEvent->hdr.Index         = SlotIndex;
    TmpEvent->hdr.Type          = SDOBJ_EVENT;
    TmpEvent->hdr.IdStr         = g_strdup(IdStr);
    TmpEvent->hdr.ClassStr      = ClassStr ? g_strdup(ClassStr):NULL;

    if( TmpEvent->hdr.Index > priv->MaxEventIndex )
        priv->MaxEventIndex = TmpEvent->hdr.Index;

    TmpEvent->StartActorIndx    = SAPtr->hdr.Index;
    TmpEvent->EndActorIndx      = 0;
    
    TmpEvent->ArrowDir          = ARROWDIR_STEP;

    if( Label )
        TmpEvent->UpperText.Str  = strdup(Label);

    sqd_layout_add_event_common( sb, TmpEvent);

}

gboolean
sqd_layout_add_external_event( SQDLayout *sb, gchar *IdStr, gchar *ClassStr, int SlotIndex, gchar *ActorId, gchar *Label, gboolean FromFlag )
{
	SQDLayoutPrivate *priv;
    SQD_EVENT        *TmpEvent;
    SQD_EVENT_LAYER  *Layer;
    guint32          TmpMask, i;
    SQD_ACTOR        *SAPtr;
    SQD_ACTOR        *EAPtr;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Make sure the ID isn't already in use
    if( g_hash_table_lookup(priv->IdTable, IdStr) != NULL )
    {
        g_error("Sequence object id \"%s\" already exists. Ids must be unique.\n", IdStr);
        return TRUE;
    }

    // Lookup the actor
    SAPtr = g_hash_table_lookup(priv->IdTable, ActorId);
    g_print("Actor Lookup: 0x%x, %d, %d, %s\n", SAPtr, SAPtr->hdr.Index, SAPtr->hdr.Type, SAPtr->hdr.IdStr); 
    if( SAPtr == NULL )
    {
        g_error("Couldn't find start actor with id \"%s\".\n", ActorId);
        return TRUE;
    }
    if( SAPtr->hdr.Type != SDOBJ_ACTOR )
    {
        g_error("Object with id \"%s\" is not of the required actor type.\n", ActorId);
        return TRUE;
    }

    TmpEvent = malloc( sizeof(SQD_EVENT) );

    memset(TmpEvent, 0, sizeof(SQD_EVENT));

    TmpEvent->hdr.Index         = SlotIndex;
    TmpEvent->hdr.Type          = SDOBJ_EVENT;
    TmpEvent->hdr.IdStr         = g_strdup(IdStr);
    TmpEvent->hdr.ClassStr      = ClassStr ? g_strdup(ClassStr):NULL;

    if( TmpEvent->hdr.Index > priv->MaxEventIndex )
        priv->MaxEventIndex = TmpEvent->hdr.Index;

    TmpEvent->StartActorIndx    = SAPtr->hdr.Index;
    TmpEvent->EndActorIndx      = 0;
    
    if( FromFlag )
        TmpEvent->ArrowDir = ARROWDIR_EXTERNAL_FROM;
    else
        TmpEvent->ArrowDir = ARROWDIR_EXTERNAL_TO;

    if( Label )
        TmpEvent->UpperText.Str  = strdup(Label);

    sqd_layout_add_event_common( sb, TmpEvent);

}

gboolean
sqd_layout_add_actor( SQDLayout *sb, gchar *IdStr, gchar *ClassStr, int ActorIndex, gchar *ActorTitle )
{
	SQDLayoutPrivate *priv;
    SQD_ACTOR *TmpActor;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    TmpActor = malloc( sizeof(SQD_ACTOR) );

    // Make sure the ID isn't already in use
    if( g_hash_table_lookup(priv->IdTable, IdStr) != NULL )
    {
        g_error("Sequence object id \"%s\" already exists. Ids must be unique.\n", IdStr);
        return TRUE;
    }

    TmpActor->hdr.Index           = ActorIndex;
    TmpActor->hdr.Type            = SDOBJ_ACTOR;
    TmpActor->hdr.IdStr           = g_strdup(IdStr);
    TmpActor->hdr.ClassStr        = ClassStr ? g_strdup(ClassStr):NULL;

    if( TmpActor->hdr.Index > priv->MaxActorIndex )
        priv->MaxActorIndex = TmpActor->hdr.Index;

    TmpActor->Name.Str            = NULL;
    TmpActor->Name.Width          = 0;
    TmpActor->Name.Height         = 0;

    if( ActorTitle )
        TmpActor->Name.Str        = strdup(ActorTitle);

    TmpActor->BoundsBox.Top     = 0;
    TmpActor->BoundsBox.Bottom  = 0;
    TmpActor->BoundsBox.Start   = 0;
    TmpActor->BoundsBox.End     = 0;

    TmpActor->NameBox.Top     = 0;
    TmpActor->NameBox.Bottom  = 0;
    TmpActor->NameBox.Start   = 0;
    TmpActor->NameBox.End     = 0;

    TmpActor->BaselineBox.Top     = 0;
    TmpActor->BaselineBox.Bottom  = 0;
    TmpActor->BaselineBox.Start   = 0;
    TmpActor->BaselineBox.End     = 0;

    TmpActor->StemBox.Top         = 0;
    TmpActor->StemBox.Bottom      = 0;
    TmpActor->StemBox.Start       = 0;
    TmpActor->StemBox.End         = 0;

    g_ptr_array_add(priv->Actors, TmpActor); 

    // Add this object to the ID hash table.
    g_hash_table_insert( priv->IdTable, TmpActor->hdr.IdStr, TmpActor );
    g_print("Actor Insert: 0x%x, %d, %d, %s\n", TmpActor, TmpActor->hdr.Index, TmpActor->hdr.Type, TmpActor->hdr.IdStr); 
}

gboolean
sqd_layout_add_actor_region(  SQDLayout *sb, gchar *IdStr, gchar *ClassStr, gchar *ActorId, gchar *StartEvent, gchar *EndEvent )
{
	SQDLayoutPrivate *priv;
    SQD_ACTOR_REGION *TmpRegion;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    TmpRegion = malloc( sizeof(SQD_ACTOR_REGION) );

    // Make sure the ID isn't already in use
    if( g_hash_table_lookup(priv->IdTable, IdStr) != NULL )
    {
        g_error("Sequence object id \"%s\" already exists. Ids must be unique.\n", IdStr);
        return TRUE;
    }

    TmpRegion->hdr.Index    = 0;
    TmpRegion->hdr.Type     = SDOBJ_AREGION;
    TmpRegion->hdr.IdStr    = g_strdup(IdStr);
    TmpRegion->hdr.ClassStr = ClassStr ? g_strdup(ClassStr):NULL;;

    TmpRegion->ActorRef = g_hash_table_lookup(priv->IdTable, ActorId);
    if( (TmpRegion->ActorRef == NULL) || (TmpRegion->ActorRef->hdr.Type != SDOBJ_ACTOR) )
    {
        g_error("Reference to actor with id \"%s\" was not found.\n", ActorId);
        return TRUE;
    }

    TmpRegion->SEventRef = g_hash_table_lookup(priv->IdTable, StartEvent);
    if( (TmpRegion->SEventRef == NULL) || (TmpRegion->SEventRef->hdr.Type != SDOBJ_EVENT) )
    {
        g_error("Reference to start event with id \"%s\" was not found.\n", StartEvent);
        return TRUE;
    }

    TmpRegion->EEventRef = g_hash_table_lookup(priv->IdTable, EndEvent);
    if( (TmpRegion->EEventRef == NULL) || (TmpRegion->EEventRef->hdr.Type != SDOBJ_EVENT) )
    {
        g_error("Reference to end event with id \"%s\" was not found.\n", EndEvent);
        return TRUE;
    }

    TmpRegion->BoundsBox.Top     = 0;
    TmpRegion->BoundsBox.Bottom  = 0;
    TmpRegion->BoundsBox.Start   = 0;
    TmpRegion->BoundsBox.End     = 0;

    g_ptr_array_add(priv->ActorRegions, TmpRegion); 

    // Add this object to the ID hash table.
    g_hash_table_insert( priv->IdTable, TmpRegion->hdr.IdStr, TmpRegion );
    g_print("Actor-Region Insert: 0x%x, %d, %d, %s\n", TmpRegion, TmpRegion->hdr.Index, TmpRegion->hdr.Type, TmpRegion->hdr.IdStr); 
}

gboolean
sqd_layout_add_box_region( SQDLayout *sb, gchar *IdStr, gchar *ClassStr, gchar *StartActor, gchar *EndActor, gchar *StartEvent, gchar *EndEvent)
{
	SQDLayoutPrivate *priv;
    SQD_BOX_REGION *TmpRegion;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    TmpRegion = malloc( sizeof(SQD_BOX_REGION) );

    // Make sure the ID isn't already in use
    if( g_hash_table_lookup(priv->IdTable, IdStr) != NULL )
    {
        g_error("Sequence object id \"%s\" already exists. Ids must be unique.\n", IdStr);
        return TRUE;
    }

    TmpRegion->hdr.Index    = 0;
    TmpRegion->hdr.Type     = SDOBJ_BREGION;
    TmpRegion->hdr.IdStr    = g_strdup(IdStr);
    TmpRegion->hdr.ClassStr = ClassStr ? g_strdup(ClassStr):NULL;;

    TmpRegion->SActorRef = g_hash_table_lookup(priv->IdTable, StartActor);
    if( (TmpRegion->SActorRef == NULL) || (TmpRegion->SActorRef->hdr.Type != SDOBJ_ACTOR) )
    {
        g_error("Reference to start actor with id \"%s\" was not found.\n", StartActor);
        return TRUE;
    }

    TmpRegion->EActorRef = g_hash_table_lookup(priv->IdTable, EndActor);
    if( (TmpRegion->EActorRef == NULL) || (TmpRegion->EActorRef->hdr.Type != SDOBJ_ACTOR) )
    {
        g_error("Reference to end actor with id \"%s\" was not found.\n", EndActor);
        return TRUE;
    }

    TmpRegion->SEventRef = g_hash_table_lookup(priv->IdTable, StartEvent);
    if( (TmpRegion->SEventRef == NULL) || (TmpRegion->SEventRef->hdr.Type != SDOBJ_EVENT) )
    {
        g_error("Reference to start event with id \"%s\" was not found.\n", StartEvent);
        return TRUE;
    }

    TmpRegion->EEventRef = g_hash_table_lookup(priv->IdTable, EndEvent);
    if( (TmpRegion->EEventRef == NULL) || (TmpRegion->EEventRef->hdr.Type != SDOBJ_EVENT) )
    {
        g_error("Reference to end event with id \"%s\" was not found.\n", EndEvent);
        return TRUE;
    }

    TmpRegion->BoundsBox.Top     = 0;
    TmpRegion->BoundsBox.Bottom  = 0;
    TmpRegion->BoundsBox.Start   = 0;
    TmpRegion->BoundsBox.End     = 0;

    g_ptr_array_add(priv->BoxRegions, TmpRegion); 

    // Add this object to the ID hash table.
    g_hash_table_insert( priv->IdTable, TmpRegion->hdr.IdStr, TmpRegion );
    g_print("Box-Region Insert: 0x%x, %d, %d, %s\n", TmpRegion, TmpRegion->hdr.Index, TmpRegion->hdr.Type, TmpRegion->hdr.IdStr); 
}

gboolean
sqd_layout_add_note( SQDLayout *sb, gchar *IdStr, gchar *ClassStr, int NoteIndex, int NoteType, gchar *RefId, char *NoteText)
{
	SQDLayoutPrivate *priv;
    SQD_NOTE *TmpNote;
    SQD_OBJ  *RefObj;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Make sure the ID isn't already in use
    if( g_hash_table_lookup(priv->IdTable, IdStr) != NULL )
    {
        g_error("Sequence object id \"%s\" already exists. Ids must be unique.\n", IdStr);
        return TRUE;
    }

    // Make sure the reference is to something valid
    switch(NoteType)
    {
        case NOTE_REFTYPE_NONE:   
            RefObj = NULL;   
        break;

        case NOTE_REFTYPE_ACTOR:
            // Lookup the referenced object
            RefObj = g_hash_table_lookup(priv->IdTable, RefId);
            if( (RefObj == NULL) || (RefObj->Type != SDOBJ_ACTOR) )
            {
                g_error("Couldn't find the note, actor object with id \"%s\".\n", RefId);
                return TRUE;
            }
            g_print("RefObj ID Lookup: 0x%x, %d, %d, %s\n", RefObj, RefObj->Index, RefObj->Type, RefObj->IdStr);               
        break;
       
        case NOTE_REFTYPE_EVENT_START:   
        case NOTE_REFTYPE_EVENT_MIDDLE:  
        case NOTE_REFTYPE_EVENT_END: 
            // Lookup the referenced object
            RefObj = g_hash_table_lookup(priv->IdTable, RefId);
            if( (RefObj == NULL) || (RefObj->Type != SDOBJ_EVENT) )
            {
                g_error("Couldn't find the note, event object with id \"%s\".\n", RefId);
                return TRUE;
            }
            g_print("RefObj ID Lookup: 0x%x, %d, %d, %s\n", RefObj, RefObj->Index, RefObj->Type, RefObj->IdStr);                   
        break;

        case NOTE_REFTYPE_VSPAN:  
            // Lookup the referenced object
            RefObj = g_hash_table_lookup(priv->IdTable, RefId);
            if( (RefObj == NULL) || (RefObj->Type != SDOBJ_AREGION) )
            {
                g_error("Couldn't find the note, actor-region object with id \"%s\".\n", RefId);
                return TRUE;
            }
            g_print("RefObj ID Lookup: 0x%x, %d, %d, %s\n", RefObj, RefObj->Index, RefObj->Type, RefObj->IdStr);                   
        break;

        case NOTE_REFTYPE_BOXSPAN:       
            // Lookup the referenced object
            RefObj = g_hash_table_lookup(priv->IdTable, RefId);
            if( (RefObj == NULL) || (RefObj->Type != SDOBJ_BREGION) )
            {
                g_error("Couldn't find the note, box-region object with id \"%s\".\n", RefId);
                return TRUE;
            }
            g_print("RefObj ID Lookup: 0x%x, %d, %d, %s\n", RefObj, RefObj->Index, RefObj->Type, RefObj->IdStr);                   
        break;
    }

    TmpNote = malloc( sizeof(SQD_NOTE) );

    TmpNote->hdr.Index      = NoteIndex;
    TmpNote->hdr.Type       = SDOBJ_NOTE;
    TmpNote->hdr.IdStr      = g_strdup(IdStr);
    TmpNote->hdr.ClassStr   = ClassStr ? g_strdup(ClassStr):NULL;;

    if( TmpNote->hdr.Index > priv->MaxNoteIndex )
        priv->MaxNoteIndex = TmpNote->hdr.Index;

    TmpNote->Text.Str            = NULL;
    TmpNote->Text.Width          = 0;
    TmpNote->Text.Height         = 0;

    TmpNote->Text.Str            = NULL;
    if( NoteText )
    {
        TmpNote->Text.Str        = strdup(NoteText);
    }

    TmpNote->BoundsBox.Top       = 0;
    TmpNote->BoundsBox.Bottom    = 0;
    TmpNote->BoundsBox.Start     = 0;
    TmpNote->BoundsBox.End       = 0;

    TmpNote->ReferenceType       = NoteType;

    TmpNote->Height              = 0;

    TmpNote->RefObj              = RefObj;
    TmpNote->RefFirstTop         = 0;
    TmpNote->RefFirstStart       = 0;
    TmpNote->RefLastTop          = 0;
    TmpNote->RefLastStart        = 0;

    g_ptr_array_add(priv->Notes, TmpNote); 

    // Add this object to the ID hash table.
    g_hash_table_insert( priv->IdTable, TmpNote->hdr.IdStr, TmpNote );

}


gboolean 
sqd_layout_set_presentation_parameter( SQDLayout *sb, gchar *ParamStr, gchar *ValueStr, gchar *ClassStr )
{
	SQDLayoutPrivate *priv;
    gchar            *PStr;
    SQD_P_PARAM      *PParam;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    // Build the Parameter ID String
    if(ClassStr)
        PStr = g_strdup_printf("%s.%s", ClassStr, ParamStr);
    else
        PStr = g_strdup(ParamStr);

    // Check if the Parameter already has a value
    PParam = g_hash_table_lookup(priv->PTable, PStr);
    if( PParam != NULL )
    {
        // Parameter already exists, just modify the value.
        if( PParam->ValueStr )
            g_free( PParam->ValueStr );

        PParam->ValueStr = g_strdup(ValueStr);

        g_free(PStr);

        return FALSE;
    }

    // Parameter does not yet exist, create and init the parameter.
    PParam = g_malloc(sizeof(SQD_P_PARAM));

    if(PParam == NULL)
    {
        g_error("Out of memory!\n");
        return TRUE;
    }

    // Init the new parameter.
    PParam->ParamStr = PStr;
    PParam->ClassStr = g_strdup(ClassStr);
    PParam->ValueStr = g_strdup(ValueStr);

    // Install the parameter in the table
    g_hash_table_insert( priv->PTable, PParam->ParamStr, PParam );

    return FALSE;
}

gboolean
sqd_layout_generate_pdf( SQDLayout *sb, gchar *FilePath )
{
	SQDLayoutPrivate *priv;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    priv->surface = cairo_pdf_surface_create(FilePath, priv->Width, priv->Height);
    priv->cr = cairo_create (priv->surface);

    cairo_set_source_rgb(priv->cr, 0, 0, 0);

    sqd_layout_arrange_diagram(sb);
    sqd_layout_draw_diagram(sb);

    cairo_show_page(priv->cr);
    cairo_destroy(priv->cr);
    cairo_surface_destroy(priv->surface);

    priv->cr      = NULL;
    priv->surface = NULL;
}

gboolean
sqd_layout_generate_png( SQDLayout *sb, gchar *FilePath )
{
	SQDLayoutPrivate *priv;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    priv->surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, priv->Width, priv->Height);
    priv->cr = cairo_create (priv->surface);

    cairo_set_source_rgb(priv->cr, 0, 0, 0);

    sqd_layout_arrange_diagram(sb);
    sqd_layout_draw_diagram(sb);

    cairo_show_page(priv->cr);

    cairo_surface_write_to_png(priv->surface, FilePath);
    cairo_destroy(priv->cr);
    cairo_surface_destroy(priv->surface);

    priv->cr      = NULL;
    priv->surface = NULL;
}

gboolean
sqd_layout_generate_svg( SQDLayout *sb, gchar *FilePath )
{
	SQDLayoutPrivate *priv;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

    priv->surface = cairo_svg_surface_create(FilePath, priv->Width, priv->Height);
    priv->cr = cairo_create (priv->surface);

    cairo_set_source_rgb(priv->cr, 0, 0, 0);

    sqd_layout_arrange_diagram(sb);
    sqd_layout_draw_diagram(sb);

    cairo_show_page(priv->cr);
    cairo_destroy(priv->cr);
    cairo_surface_destroy(priv->surface);

    priv->cr      = NULL;
    priv->surface = NULL;
}



