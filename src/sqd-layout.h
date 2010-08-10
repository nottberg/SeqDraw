/**
 * sqd-layout.h
 *
 * Defines a GObject representing a sequence diagram layout.
 *
 * (c) 2010, Curtis Nottberg
 *
 * Authors:
 *   Curtis Nottberg
 */

#include <glib.h>
#include <glib-object.h>


#ifndef __SQD_LAYOUT_H__
#define __SQD_LAYOUT_H__

enum NoteReferenceTypes
{
    NOTE_REFTYPE_NONE,          // Just a general note that doesn't reference a specific diagram feature.
    NOTE_REFTYPE_ACTOR,         // References the a specific Actor.
    NOTE_REFTYPE_EVENT_START,   // Reference the starting point of a specific event.
    NOTE_REFTYPE_EVENT_MIDDLE,  // Reference the middle point of a specific event.
    NOTE_REFTYPE_EVENT_END,     // Reference the end point of a specific event.
    NOTE_REFTYPE_VSPAN,         // Reference to a Vertical Span of events.
    NOTE_REFTYPE_BOXSPAN,       // Group events into a box. Reference to the box.
};

#define G_TYPE_SQD_LAYOUT			(sqd_layout_get_type ())
#define G_SQD_LAYOUT(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), G_TYPE_SQD_LAYOUT, SQDLayout))
#define G_SQD_LAYOUT_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_TDA_HUDSON, SQDLayoutClass))

typedef struct _SQDLayout		    SQDLayout;
typedef struct _SQDLayoutClass	SQDLayoutClass;

struct _SQDLayout
{
	GObject parent;
};

struct _SQDLayoutClass
{
	GObjectClass parent_class;

	/* Signal handlers */
	void (*req_complete)  (SQDLayout *HudsonObj, gpointer param);

    guint req_complete_id;
};

SQDLayout *g_sqd_layout_new (void);

gboolean sqd_layout_add_event( SQDLayout *sb, char *IdStr, int SlotIndex, gchar *StartActorId, gchar *EndActorId, char *TopLabel, char *BottomLabel);
gboolean sqd_layout_add_step_event( SQDLayout *sb, gchar *IdStr, int SlotIndex, gchar *ActorId, gchar *Label);
gboolean sqd_layout_add_external_event( SQDLayout *sb, gchar *IdStr, int SlotIndex, gchar *ActorId, gchar *Label, gboolean FromFlag);

gboolean sqd_layout_add_actor( SQDLayout *sb, char *IdStr, int ActorIndex, char *ActorTitle);
gboolean sqd_layout_add_note( SQDLayout *sb, char *IdStr, int NoteIndex, int NoteType, gchar *RefId, char *NoteText);

gboolean sqd_layout_generate_pdf( SQDLayout *sb, gchar *FilePath );
gboolean sqd_layout_generate_png( SQDLayout *sb, gchar *FilePath );
gboolean sqd_layout_generate_svg( SQDLayout *sb, gchar *FilePath );

G_END_DECLS

#endif
