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
};

typedef struct SeqDrawObjectHdr
{
    guint8  Type;
    guint8  Index;
    gchar  *IdStr; 
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
    gint MaxActorIndex;
    gdouble MaxActorHeight;
    gdouble ActorWidth;

    // Event Stats
    gint MaxEventIndex;

    // Note Stats
    gint MaxNoteIndex;

    // Actor, Event, Note Lists
    GPtrArray *Notes;
    GPtrArray *Actors;
    GArray *EventLayers;

    //GList  *Events;
    cairo_surface_t *surface;
    cairo_t         *cr;

    gboolean      dispose_has_run;

    // Keep a hash table of assigned IDs
    GHashTable *IdTable;
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

    priv->Title.Str       = NULL;
    priv->Description.Str = NULL;

    priv->surface = NULL;
    priv->cr      = NULL;
        
    priv->IdTable = g_hash_table_new(g_str_hash, g_str_equal);

    priv->dispose_has_run = FALSE;
}



static void
draw_text (cairo_t *cr)
{
#define RADIUS 50
#define N_WORDS 10
//#define FONT "Courier 10"
#define FONT "Times 10"

  PangoLayout *layout;
  PangoFontDescription *desc;
  int i;
  int pwidth, pheight;
  double cwidth, cheight;

  /* Center coordinates on the middle of the region we are drawing
   */
  //cairo_translate (cr, RADIUS, RADIUS);
  cairo_move_to (cr, 0.5, 0.5);
  //cairo_rel_move_to (cr, -0.4 , cheight + 0.2);      
  //cairo_rel_line_to (cr, cwidth + 0.8, 0);

  //cairo_rel_move_to (cr, -(0.4 + cwidth / 2) , 0);      
  cairo_rel_line_to (cr, 0, .45);

  cairo_stroke (cr);

  cairo_move_to (cr, 0.5, 0.5);
  cairo_scale(cr, 1.0, 1.0);

  /* Create a PangoLayout, set the font and text */
  layout = pango_cairo_create_layout (cr);
  
  pango_layout_set_text (layout, "Text", -1);
  desc = pango_font_description_from_string (FONT);
  pango_layout_set_font_description (layout, desc);
  pango_font_description_free (desc);

  /* Inform Pango to re-layout the text with the new transformation */
  pango_cairo_update_layout (cr, layout);

  pango_layout_get_size (layout, &pwidth, &pheight);

  printf("Pango Extents: %g %g\n", ((double)pwidth / PANGO_SCALE), ((double)pheight / PANGO_SCALE) ); 

  cwidth = ((double)pwidth / PANGO_SCALE); /// (8.0*72);
  cheight = ((double)pheight / PANGO_SCALE); /// (11.0*72) ;

  printf("Pango Extents 2: %g %g\n", cwidth, cheight ); 

  //cairo_rel_move_to (cr, -(cwidth / 2), 0);
  //cairo_move_to (cr, 0.5, 0.5);
  pango_cairo_show_layout (cr, layout);

  cairo_scale(cr, (8*72), (11*72));


  /* Draw the layout N_WORDS times in a circle */
#if 0
  for (i = 0; i < N_WORDS; i++)
    {
      int width, height;
      double angle = (360. * i) / N_WORDS;
      double red;

      cairo_save (cr);

      /* Gradient from red at angle == 60 to blue at angle == 240 */
      //red   = (1 + cos ((angle - 60) * G_PI / 180.)) / 2;
      //cairo_set_source_rgb (cr, red, 0, 1.0 - red);

      //cairo_rotate (cr, angle * G_PI / 180.);
    
      /* Inform Pango to re-layout the text with the new transformation */
      pango_cairo_update_layout (cr, layout);
    
      pango_layout_get_size (layout, &width, &height);
      cairo_move_to (cr, - ((double)width / PANGO_SCALE) / 2, - RADIUS);
      pango_cairo_show_layout (cr, layout);
      
      cairo_rel_line_to (cr, width, height);
      cairo_set_line_width (cr, 4);
      cairo_stroke (cr);


      cairo_restore (cr);
    }
#endif

  /* free the layout object */
  g_object_unref (layout);
}



static void
sqd_layout_draw_actor ( SQDLayout *sb, int ActorIndex, char *ActorTitle)
{
	SQDLayoutPrivate *priv;
  PangoLayout *layout;
  PangoFontDescription *desc;
  int i;
  int pwidth, pheight;
  double cwidth, cheight;

	priv = SQD_LAYOUT_GET_PRIVATE (sb);

  /* Center coordinates on the middle of the region we are drawing
   */
  cairo_move_to (priv->cr, priv->SeqBox.Start + (ActorIndex * priv->SeqBox.End), priv->SeqBox.Top);

  /* Create a PangoLayout, set the font and text */
  layout = pango_cairo_create_layout (priv->cr);
  
  pango_layout_set_text (layout, ActorTitle, -1);
  desc = pango_font_description_from_string ("Times 10");
  pango_layout_set_font_description (layout, desc);
  pango_font_description_free (desc);

  pango_layout_get_size (layout, &pwidth, &pheight);

  printf("Pango Extents: %g %g\n", ((double)pwidth / PANGO_SCALE), ((double)pheight / PANGO_SCALE) ); 

  cwidth = ((double)pwidth / PANGO_SCALE); /// (8.0*72);
  cheight = ((double)pheight / PANGO_SCALE); /// (11.0*72) ;

  printf("Pango Extents 2: %g %g\n", cwidth, cheight ); 

  cairo_rel_move_to (priv->cr, -(cwidth / 2), 0);
  pango_cairo_show_layout (priv->cr, layout);

  cairo_rel_move_to (priv->cr, 0, cheight);
  cairo_rel_line_to (priv->cr, cwidth, 0);
  cairo_rel_move_to (priv->cr, -(cwidth/2), 0);
  cairo_rel_line_to (priv->cr, 0, (priv->SeqBox.Bottom - priv->SeqBox.Top));

  //cairo_set_line_width (priv->cr, 4);
  cairo_stroke (priv->cr);


  /* free the layout object */
  g_object_unref (layout);
}

static void
sqd_layout_draw_arrow ( SQDLayout *sb, int EventIndex, int StartActorIndex, int EndActorIndex, char *TopText, char *BottomText)
{
	SQDLayoutPrivate *priv;
  PangoLayout *layout;
  PangoFontDescription *desc;
  int i;
  int pwidth, pheight;
  double cwidth, cheight;

  double StartX, EndX, EventY;
  double ArrowX, ArrowY;

    priv = SQD_LAYOUT_GET_PRIVATE (sb);

  StartX = priv->SeqBox.Start + (StartActorIndex * priv->SeqBox.End);
  EndX   = priv->SeqBox.Start + (EndActorIndex * priv->SeqBox.End);
  //EventY = priv->SeqBox.Top + (EventIndex * priv->EventHeight) + 72;

  ArrowX = (StartX > EndX) ? (EndX + 3.0) : (EndX - 3.0);  
  ArrowY = 3.0;

  /* Center coordinates on the middle of the region we are drawing
   */

  cairo_move_to (priv->cr, StartX, EventY);

  cairo_line_to (priv->cr, EndX, EventY);
  cairo_line_to (priv->cr, ArrowX, (EventY - ArrowY));
  cairo_move_to (priv->cr, EndX, EventY);
  cairo_line_to (priv->cr, ArrowX, (EventY + ArrowY));

  cairo_stroke (priv->cr);

  /* Create a PangoLayout, set the font and text */
  //layout = pango_cairo_create_layout (priv->cr);
  
  //pango_layout_set_text (layout, ActorTitle, -1);
  //desc = pango_font_description_from_string ("Times 10");
  //pango_layout_set_font_description (layout, desc);
  //pango_font_description_free (desc);

  //pango_layout_get_size (layout, &pwidth, &pheight);

  //printf("Pango Extents: %g %g\n", ((double)pwidth / PANGO_SCALE), ((double)pheight / PANGO_SCALE) ); 

  //cwidth = ((double)pwidth / PANGO_SCALE); /// (8.0*72);
  //cheight = ((double)pheight / PANGO_SCALE); /// (11.0*72) ;

  //printf("Pango Extents 2: %g %g\n", cwidth, cheight ); 

  //cairo_rel_move_to (priv->cr, -(cwidth / 2), 0);
  //pango_cairo_show_layout (priv->cr, layout);


  //cairo_set_line_width (priv->cr, 4);

  /* free the layout object */
  //g_object_unref (layout);
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

    desc = pango_font_description_from_string (FONT);
    pango_layout_set_font_description (layout, desc);
    pango_font_description_free (desc);

    pango_layout_set_markup (layout, Text->Str, -1);

    pango_layout_get_size (layout, &pwidth, &pheight);

    Text->Width  = ((double)pwidth  / PANGO_SCALE); 
    Text->Height = ((double)pheight / PANGO_SCALE); 

    // free the layout object 
    g_object_unref (layout);

}

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

        sqd_layout_measure_text(sb, &Actor->Name, ActorTextWidth);

        printf("Pango Actor Extents: %g %g\n", Actor->Name.Width, Actor->Name.Height); 

        if( Actor->Name.Height > priv->MaxActorHeight )
            priv->MaxActorHeight = Actor->Name.Height;   

        if( Actor->Name.Width > ActorMaxTextWidth )
            ActorMaxTextWidth = Actor->Name.Width;        
     
    }

    printf("Pango Actor Maxs: %g %g\n", priv->MaxActorHeight, ActorMaxTextWidth); 

    // Layout the Actors
    for (i = 0; i <= priv->MaxActorIndex; i++)
    {
        Actor = g_ptr_array_index(priv->Actors, i);

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
    *Top   = Actor->BaselineBox.Bottom + priv->ElementPad;
    *Start = Actor->StemBox.End + priv->ElementPad;
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

        sqd_layout_measure_text(sb, &Note->Text, NoteTextWidth);

        printf("Pango Note Extents: %g %g\n", Note->Text.Width, Note->Text.Height);      

        Note->BoundsBox.Top    = NoteTop;
        Note->BoundsBox.Bottom = NoteTop + Note->Text.Height + (2 * priv->TextPad);
        Note->BoundsBox.Start  = priv->NoteBox.Start;
        Note->BoundsBox.End    = priv->NoteBox.End;

        debug_box_print("Note Box", &Note->BoundsBox);

        NoteTop = (Note->BoundsBox.Bottom + priv->ElementPad);
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
                    Event->StemBox.End     = StartActor->StemBox.Start + priv->ActorWidth/5.0;
                    
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
                    case ARROWDIR_EXTERNAL_TO:
                        exit(-2);
                    break;

                    case ARROWDIR_EXTERNAL_FROM:
                        exit(-2);
                    break;

                    case ARROWDIR_STEP:
                        exit(-2);
                    break;

                    case ARROWDIR_LEFT_TO_RIGHT:
                        // Get a point at the baseline and stem intersection.
                        *Top   = Event->StemBox.Top;
                        *Start = Event->StemBox.Start;
                    break;

                    case ARROWDIR_RIGHT_TO_LEFT:
                        // Get a point at the baseline and stem intersection.
                        *Top   = Event->StemBox.Top;
                        *Start = Event->StemBox.End;
                    break;
                }
            break;
  
            case NOTE_REFTYPE_EVENT_MIDDLE:  
                // Get a point at the baseline and stem intersection.
                *Top   = Event->StemBox.Top;
                *Start = ((Event->StemBox.End - Event->StemBox.Start)/2.0) + Event->StemBox.Start;
            break;

            case NOTE_REFTYPE_EVENT_END: 
                switch ( Event->ArrowDir )
                {
                    case ARROWDIR_EXTERNAL_TO:
                        exit(-2);
                    break;

                    case ARROWDIR_EXTERNAL_FROM:
                        exit(-2);
                    break;

                    case ARROWDIR_STEP:
                        exit(-2);
                    break;

                    case ARROWDIR_LEFT_TO_RIGHT:
                        // Get a point at the baseline and stem intersection.
                        *Top   = Event->StemBox.Top;
                        *Start = Event->StemBox.End;
                    break;

                    case ARROWDIR_RIGHT_TO_LEFT:
                        // Get a point at the baseline and stem intersection.
                        *Top   = Event->StemBox.Top;
                        *Start = Event->StemBox.Start;
                    break;
                }
            break;
    } // Ref Type switch
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

                // End it at the middle of the event arrow.
                sqd_layout_get_actor_point( sb, Note->RefObj, &Note->RefLastTop, &Note->RefLastStart );
        
            break;

            // Reference a specific event.
            case NOTE_REFTYPE_EVENT_START:   
            case NOTE_REFTYPE_EVENT_MIDDLE:  
            case NOTE_REFTYPE_EVENT_END:     

                // Get the end-point for the reference.
                sqd_layout_get_event_point( sb, Note->RefObj, Note->ReferenceType, &Note->RefLastTop, &Note->RefLastStart );

            break;
           
            // Reference to a Vertical Span of events.
            case NOTE_REFTYPE_VSPAN:         

            break;

            // Group events into a box. Reference to the box.
            case NOTE_REFTYPE_BOXSPAN:       

            break;
        } // Ref Type switch
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
        priv->TitleBar.Start   = priv->TitleBox.Start;
        priv->TitleBar.End     = priv->TitleBox.End;
        priv->TitleBar.Top     = priv->TitleBox.Top + priv->ElementPad;

        sqd_layout_measure_text(sb, &priv->Title, (priv->TitleBar.End - priv->TitleBar.Start - (2 * priv->TextPad)));


        priv->TitleBar.Bottom  = priv->TitleBar.Top + priv->Title.Height + (2 * priv->TextPad);
        priv->TitleBox.Bottom  = priv->TitleBar.Bottom + priv->ElementPad;

        printf("Pango Title Extents: %g %g\n", priv->Title.Width, priv->Title.Height); 
        debug_box_print("TitleBar", &priv->TitleBar);
        debug_box_print("TitleBox", &priv->TitleBox);

    }

    // Default to not having a Description
    priv->DescriptionBox.Start   = priv->Margin;
    priv->DescriptionBox.End     = priv->Width - priv->Margin;
    priv->DescriptionBox.Top     = priv->TitleBox.Bottom;
    priv->DescriptionBox.Bottom  = priv->TitleBox.Bottom;

    // Determine the amount of space needed for the description block
    if( priv->Description.Str )
    {
        sqd_layout_measure_text(sb, &priv->Description, (priv->DescriptionBox.End - priv->DescriptionBox.Start - (2 * priv->TextPad)));

        priv->DescriptionBox.Bottom  = priv->TitleBox.Bottom + priv->Description.Height + (2 * priv->ElementPad) + (2 * priv->TextPad);

        printf("Pango Description Extents: %g %g\n", priv->Description.Width,priv->Description.Height);  
        debug_box_print("DescriptionBox", &priv->DescriptionBox);

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

    desc = pango_font_description_from_string (FONT);
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

        // Draw the Text bounding box.
        cairo_set_source_rgb (priv->cr, 0.95, 0.7, 0.95);

        cairo_rectangle(priv->cr, Actor->NameBox.Start, Actor->NameBox.Top, 
                            (Actor->NameBox.End - Actor->NameBox.Start),
                            (Actor->NameBox.Bottom - Actor->NameBox.Top));

        cairo_fill (priv->cr);


        // Draw the Actor Title
        // Center it over the Stem
        cairo_set_source_rgb (priv->cr, 0, 0, 0);

        cairo_move_to (priv->cr, 
                       ((Actor->StemBox.Start + priv->LineWidth) - (Actor->Name.Width / 2.0)),
                       (Actor->NameBox.Top + priv->TextPad));

        sqd_layout_draw_text( sb, &Actor->Name, ActorTextWidth );

    
        // Draw the Baseline
        cairo_move_to (priv->cr, Actor->BaselineBox.Start, Actor->BaselineBox.Top + (priv->LineWidth/2.0));
        cairo_line_to (priv->cr, Actor->BaselineBox.End, Actor->BaselineBox.Top + (priv->LineWidth/2.0));

        cairo_stroke (priv->cr);


        // Draw the Stem
        cairo_save(priv->cr);
        cairo_set_source_rgba (priv->cr, 0.4, 0.4, 0.4, 0.5);
        //cairo_set_dash (priv->cr, dashes, ndash, offset);

        cairo_move_to (priv->cr, Actor->StemBox.Start + (priv->LineWidth/2.0), Actor->StemBox.Top);
        cairo_line_to (priv->cr, Actor->StemBox.Start + (priv->LineWidth/2.0), Actor->StemBox.Bottom);

        cairo_stroke (priv->cr);

        cairo_restore(priv->cr);     
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

            // Calculate the arrow length so that available space for text layout can be calculated.
            switch ( Event->ArrowDir )
            {
                case ARROWDIR_EXTERNAL_TO:

                    cairo_set_source_rgb (priv->cr, 0, 0, 0);

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

                    cairo_set_source_rgb (priv->cr, 0, 0, 0);

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

                    // Draw the boxes for debug
                    cairo_set_source_rgba (priv->cr, 0.6, 0.2, 0.2, 0.7);
                    cairo_rectangle(priv->cr, Event->EventBox.Start, Event->EventBox.Top, 
                                             (Event->EventBox.End - Event->EventBox.Start), (Event->EventBox.Bottom - Event->EventBox.Top));
     
                    cairo_stroke (priv->cr);
                                          
                    cairo_set_source_rgba (priv->cr, 0.2, 0.6, 0.2, 0.7);
                    cairo_rectangle(priv->cr, Event->StemBox.Start, Event->StemBox.Top, 
                                             (Event->StemBox.End - Event->StemBox.Start), (Event->StemBox.Bottom - Event->StemBox.Top));

                    cairo_stroke (priv->cr);

                    cairo_set_source_rgb (priv->cr, 0, 0, 0);
                  
                    // Draw the Stem
                    cairo_move_to (priv->cr, Event->StemBox.Start, Event->StemBox.Top);
                    cairo_curve_to(priv->cr, Event->StemBox.End, Event->StemBox.Top, Event->StemBox.End, Event->StemBox.Bottom,
                                                Event->StemBox.Start, Event->StemBox.Bottom);

                    cairo_stroke (priv->cr);

                    cairo_move_to (priv->cr, Event->StemBox.End - priv->ArrowLength, Event->StemBox.Top + (priv->LineWidth/2.0) - (priv->ArrowWidth/2.0));
                    cairo_line_to (priv->cr, Event->StemBox.End, Event->StemBox.Top + (priv->LineWidth/2.0));
                    cairo_line_to (priv->cr, Event->StemBox.End - priv->ArrowLength, Event->StemBox.Top + (priv->LineWidth/2.0) + (priv->ArrowWidth/2.0));

                    cairo_stroke (priv->cr);
                break;

                case ARROWDIR_LEFT_TO_RIGHT:

                    cairo_set_source_rgb (priv->cr, 0, 0, 0);

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

                    cairo_set_source_rgb (priv->cr, 0, 0, 0);

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
           
            Element = g_list_next(Element);
        } // Event Layout Loop
    } // Event Layer Loop 

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

        // Draw the Text bounding box.
        cairo_set_source_rgb (priv->cr, 0.95, 0.7, 0.95);

        cairo_rectangle(priv->cr, Note->BoundsBox.Start, Note->BoundsBox.Top, 
                            (Note->BoundsBox.End - Note->BoundsBox.Start),
                            (Note->BoundsBox.Bottom - Note->BoundsBox.Top));

        cairo_fill (priv->cr);


        // Draw the Actor Title
        // Center it over the Stem
        cairo_set_source_rgb (priv->cr, 0, 0, 0);

        cairo_move_to (priv->cr, (Note->BoundsBox.Start + priv->TextPad), (Note->BoundsBox.Top + priv->TextPad));

        sqd_layout_draw_text( sb, &Note->Text, NoteTextWidth );
     
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

        // Draw the Actor Title
        // Center it over the Stem
        cairo_save(priv->cr);
        cairo_set_source_rgba (priv->cr, 0.6, 0.6, 0.6, 0.6);
        cairo_set_line_cap  (priv->cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_dash (priv->cr, dashes, ndash, offset);

        cairo_move_to (priv->cr, Note->BoundsBox.Start, Note->BoundsBox.Top);
        cairo_line_to (priv->cr, Note->RefLastStart, Note->RefLastTop ); 
        cairo_stroke (priv->cr);

        cairo_restore(priv->cr);

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
        priv->TitleBar.Start   = priv->TitleBox.Start;

        cairo_set_source_rgb (priv->cr, 0.95, 0.7, 0.95);

        cairo_rectangle(priv->cr, priv->TitleBar.Start, priv->TitleBar.Top, 
                            (priv->TitleBar.End - priv->TitleBar.Start),
                            (priv->TitleBar.Bottom - priv->TitleBar.Top));

        cairo_fill (priv->cr);

        cairo_set_source_rgb (priv->cr, 0, 0, 0);

        cairo_move_to (priv->cr, (priv->TitleBar.Start + priv->TextPad), (priv->TitleBar.Top + priv->TextPad));

        sqd_layout_draw_text( sb, &priv->Title, (priv->TitleBar.End - priv->TitleBar.Start) );
    }

    // Determine the amount of space needed for the description block
    if( priv->Description.Str )
    {
        cairo_move_to (priv->cr, 
                        (priv->DescriptionBox.Start + priv->TextPad), 
                        (priv->DescriptionBox.Top + priv->ElementPad + priv->TextPad));

        sqd_layout_draw_text( sb, &priv->Description, (priv->DescriptionBox.End - priv->DescriptionBox.Start) );
    }

    sqd_layout_draw_actors(sb);

    sqd_layout_draw_events(sb);

    sqd_layout_draw_notes(sb);

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
sqd_layout_add_event( SQDLayout *sb, gchar *IdStr, int SlotIndex, gchar *StartActorId, gchar *EndActorId, char *TopLabel, char *BottomLabel)
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
sqd_layout_add_step_event( SQDLayout *sb, gchar *IdStr, int SlotIndex, gchar *ActorId, gchar *Label)
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
sqd_layout_add_external_event( SQDLayout *sb, gchar *IdStr, int SlotIndex, gchar *ActorId, gchar *Label, gboolean FromFlag )
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
sqd_layout_add_actor( SQDLayout *sb, gchar *IdStr, int ActorIndex, char *ActorTitle)
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

    TmpActor->hdr.Index               = ActorIndex;
    TmpActor->hdr.Type                = SDOBJ_ACTOR;
    TmpActor->hdr.IdStr               = g_strdup(IdStr);

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
sqd_layout_add_note( SQDLayout *sb, gchar *IdStr, int NoteIndex, int NoteType, gchar *RefId, char *NoteText)
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
                g_error("Couldn't find the note, actor object with id \"%s\".\n", RefId);
                return TRUE;
            }
            g_print("RefObj ID Lookup: 0x%x, %d, %d, %s\n", RefObj, RefObj->Index, RefObj->Type, RefObj->IdStr);                   
        break;

        case NOTE_REFTYPE_VSPAN:  
            g_error("ref Vspan not supported.\n");       
        break;

        case NOTE_REFTYPE_BOXSPAN:       
            g_error("ref BoxSpan not supported.\n");       
        break;
    }

    TmpNote = malloc( sizeof(SQD_NOTE) );

    TmpNote->hdr.Index           = NoteIndex;
    TmpNote->hdr.Type            = SDOBJ_NOTE;
    TmpNote->hdr.IdStr           = g_strdup(IdStr);

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



