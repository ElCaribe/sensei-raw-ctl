/*
 * sensei-raw-ctl-gui.c: SteelSeries Sensei Raw control utility - GTK+ GUI
 *
 * Very tightly coupled with the sensei-raw-ctl utility.
 *
 * Copyright (c) 2013, Přemysl Janouch <p.janouch@gmail.com>
 * All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <stdlib.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <sys/types.h>
#include <sys/wait.h>

#include "config.h"

/** User interface string for GtkBuilder. */
extern const char ui[];

/* To translate combo box entries into sensei-raw-ctl arguments. */
static gchar *pulsation_list[] = { "steady", "slow", "medium", "fast", NULL };
static gchar *intensity_list[] = { "off", "low", "medium", "high", NULL };

/* GtkNotebook pages within the UI. */
enum
{
	PAGE_PROBING,
	PAGE_NO_DEVICE,
	PAGE_SETTINGS,
	PAGE_COUNT
};

/* sensei-raw-ctl output values. */
enum
{
	OUT_INTENSITY,
	OUT_PULSATION,
	OUT_CPI_LED_OFF,
	OUT_CPI_LED_ON,
	OUT_POLLING,
	OUT_COUNT
};

// ----- User interface -------------------------------------------------------

static void
fatal (GtkWidget *parent, const gchar *message)
{
	GtkWidget *dialog = gtk_message_dialog_new (GTK_WINDOW (parent),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, _("Fatal error"));
	gtk_message_dialog_format_secondary_text
		(GTK_MESSAGE_DIALOG (dialog), "%s", message);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	gtk_main_quit ();
}

static void
set_page (GtkBuilder *builder, gint page)
{
	GtkNotebook *notebook = GTK_NOTEBOOK
		(gtk_builder_get_object (builder, "notebook"));
	gtk_notebook_set_current_page (notebook, page);
}

static gboolean
spawn_ctl (gchar **argv, gchar **out, GtkBuilder *builder)
{
	GError *error = NULL;
	gint status;
	gchar *err;

	GtkWidget *win = GTK_WIDGET (gtk_builder_get_object (builder, "win"));
	if (!g_spawn_sync (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
		out, &err, &status, &error))
	{
		fatal (win, error->message);
		g_error_free (error);
		return FALSE;
	}

	if (WIFEXITED (status) && WEXITSTATUS (status) == 0)
		return TRUE;

	if (strstr (err, "no suitable device"))
		set_page (builder, PAGE_NO_DEVICE);
	else
		fatal (win, err);

	g_clear_pointer (out, g_free);
	g_free (err);
	return FALSE;
}

static gboolean
set_combo (GtkComboBox *combo, gchar *list[], const gchar *word)
{
	gint i;
	for (i = 0; list[i]; i++)
		if (!strcmp (word, list[i]))
		{
			gtk_combo_box_set_active (combo, i);
			return TRUE;
		}
	return FALSE;
}

static gboolean
set_scale (GtkRange *scale, const gchar *word, const gchar *follows)
{
	gchar *end;
	gint64 value = g_ascii_strtoll (word, &end, 10);
	if (strcmp (end, follows))
		return FALSE;

	gtk_range_set_value (scale, value);
	return TRUE;
}

static void
load_configuration (GtkBuilder *builder)
{
	gchar *argv[] = { "pkexec",
		PROJECT_INSTALL_BINDIR "/" PROJECT_NAME, "--show", NULL };

	gchar *out;
	if (!spawn_ctl (argv, &out, builder))
		return;

	GRegex *regex = g_regex_new ("(?<=: ).*$", G_REGEX_MULTILINE, 0, NULL);
	GMatchInfo *info;
	g_regex_match (regex, out, 0, &info);

	gint line = 0;
	gchar *word = NULL;

	while (g_match_info_matches (info))
	{
		g_free (word);
		word = g_match_info_fetch (info, 0);
		switch (line++)
		{
		case OUT_INTENSITY:
			if (!set_combo (GTK_COMBO_BOX (gtk_builder_get_object
				(builder, "intensity_combo")), intensity_list, word))
				goto out;
			break;
		case OUT_PULSATION:
			if (!set_combo (GTK_COMBO_BOX (gtk_builder_get_object
				(builder, "pulsation_combo")), pulsation_list, word))
				goto out;
			break;
		case OUT_CPI_LED_OFF:
			if (!set_scale (GTK_RANGE (gtk_builder_get_object
				(builder, "cpi_off_scale")), word, ""))
				goto out;
			break;
		case OUT_CPI_LED_ON:
			if (!set_scale (GTK_RANGE (gtk_builder_get_object
				(builder, "cpi_on_scale")), word, ""))
				goto out;
			break;
		case OUT_POLLING:
			if (!set_scale (GTK_RANGE (gtk_builder_get_object
				(builder, "polling_scale")), word, "Hz"))
				goto out;
		}
		g_match_info_next (info, NULL);
	}

	set_page (builder, PAGE_SETTINGS);

out:
	g_free (word);
	g_match_info_free (info);
	g_regex_unref (regex);
	g_free (out);

	if (line != OUT_COUNT)
		fatal (GTK_WIDGET (gtk_builder_get_object (builder, "win")),
			_("Internal error"));
}

static void
retry_load (GtkBuilder *builder)
{
	set_page (builder, PAGE_PROBING);
	load_configuration (builder);
}

static void
save_configuration (GtkBuilder *builder)
{
	gchar *polling = g_strdup_printf ("%.0f", gtk_range_get_value
		(GTK_RANGE (gtk_builder_get_object (builder, "polling_scale"))));
	gchar *cpi_on  = g_strdup_printf ("%.0f", gtk_range_get_value
		(GTK_RANGE (gtk_builder_get_object (builder, "cpi_on_scale"))));
	gchar *cpi_off = g_strdup_printf ("%.0f", gtk_range_get_value
		(GTK_RANGE (gtk_builder_get_object (builder, "cpi_off_scale"))));

	GtkComboBox *combo;
	gint active;

	combo = GTK_COMBO_BOX (gtk_builder_get_object (builder, "pulsation_combo"));
	active = gtk_combo_box_get_active (combo);
	g_assert (active >= 0 && active < G_N_ELEMENTS (pulsation_list) - 1);
	gchar *pulsation = pulsation_list[active];

	combo = GTK_COMBO_BOX (gtk_builder_get_object (builder, "intensity_combo"));
	active = gtk_combo_box_get_active (combo);
	g_assert (active >= 0 && active < G_N_ELEMENTS (intensity_list) - 1);
	gchar *intensity = intensity_list[active];

	gchar *argv[] = { "pkexec", PROJECT_INSTALL_BINDIR "/" PROJECT_NAME,
		"--polling", polling, "--cpi-on", cpi_on, "--cpi-off", cpi_off,
		"--pulsation", pulsation, "--intensity", intensity, "--save", NULL };

	gchar *out;
	if (spawn_ctl (argv, &out, builder))
		g_free (out);

	g_free (polling);
	g_free (cpi_on);
	g_free (cpi_off);
}

static void
on_set_mode_normal (GtkBuilder *builder)
{
	gchar *out, *argv[] = { "pkexec",
		PROJECT_INSTALL_BINDIR "/" PROJECT_NAME, "--mode", "normal", NULL };
	if (spawn_ctl (argv, &out, builder))
		g_free (out);
}

static void
on_set_mode_legacy (GtkBuilder *builder)
{
	gchar *out, *argv[] = { "pkexec",
		PROJECT_INSTALL_BINDIR "/" PROJECT_NAME, "--mode", "compat", NULL };
	if (spawn_ctl (argv, &out, builder))
		g_free (out);
}

// ----- User interface -------------------------------------------------------

static gboolean
on_change_value (GtkRange *range, GtkScrollType scroll, gdouble value,
	gpointer user_data)
{
	GtkAdjustment *adjustment = gtk_range_get_adjustment (range);
	static const gint steps[] = { 125, 250, 500, 1000 };

	switch (scroll)
	{
		gint i;
	case GTK_SCROLL_NONE:
	case GTK_SCROLL_JUMP:
		for (i = 0; i < G_N_ELEMENTS (steps); i++)
			if (i == G_N_ELEMENTS (steps) - 1 ||
				value < (steps[i] + steps[i + 1]) / 2)
			{
				value = steps[i];
				break;
			}
		break;
	case GTK_SCROLL_STEP_BACKWARD:
	case GTK_SCROLL_PAGE_BACKWARD:
		value = gtk_adjustment_get_value (adjustment);
		for (i = 0; i < G_N_ELEMENTS (steps) - 1; i++)
			if (steps[i + 1] >= value)
			{
				value = steps[i];
				break;
			}
		break;
	case GTK_SCROLL_STEP_FORWARD:
	case GTK_SCROLL_PAGE_FORWARD:
		value = gtk_adjustment_get_value (adjustment);
		for (i = 0; i < G_N_ELEMENTS (steps); i++)
			if (steps[i] > value)
			{
				value = steps[i];
				break;
			}
		break;
	case GTK_SCROLL_START:
		value = steps[0];
		break;
	case GTK_SCROLL_END:
		value = steps[G_N_ELEMENTS (steps) - 1];
		break;
	default:
		g_assert_not_reached ();
	}

	gtk_adjustment_set_value (adjustment, value);
	return TRUE;
}

static gboolean
on_change_value_steps (GtkRange *range, GtkScrollType scroll, gdouble value,
	gpointer user_data)
{
	GtkAdjustment *adjustment = gtk_range_get_adjustment (range);
	gdouble lower = gtk_adjustment_get_lower (adjustment);
	gdouble step  = gtk_adjustment_get_step_increment (adjustment);
	value = lower + (int) ((value - lower) / step + 0.5) * step;
	gtk_adjustment_set_value (adjustment, value);
	return TRUE;
}

static gchar *
on_format_value (GtkScale *scale, gdouble value)
{
	return g_strdup_printf (_("%gHz"), value);
}

int
main (int argc, char *argv[])
{
	gtk_init (&argc, &argv);
	gtk_window_set_default_icon_name (PROJECT_NAME "-gui");

	GError *error = NULL;
	GtkBuilder *builder = gtk_builder_new ();
	if (!gtk_builder_add_from_string (builder, ui, -1, &error))
	{
		g_printerr ("%s: %s\n", _("Error"), error->message);
		exit (EXIT_FAILURE);
	}

	GtkWidget *win = GTK_WIDGET (gtk_builder_get_object (builder, "win"));
	g_signal_connect (win, "destroy", G_CALLBACK (gtk_main_quit), NULL);
	g_signal_connect_swapped (win, "map-event",
		G_CALLBACK (load_configuration), builder);
	gtk_widget_show_all (win);

	g_signal_connect (gtk_builder_get_object (builder, "polling_scale"),
		"change-value", G_CALLBACK (on_change_value), NULL);
	g_signal_connect (gtk_builder_get_object (builder, "polling_scale"),
		"format-value", G_CALLBACK (on_format_value), NULL);

	g_signal_connect (gtk_builder_get_object (builder, "cpi_off_scale"),
		"change-value", G_CALLBACK (on_change_value_steps), NULL);
	g_signal_connect (gtk_builder_get_object (builder, "cpi_on_scale"),
		"change-value", G_CALLBACK (on_change_value_steps), NULL);

	g_signal_connect_swapped (gtk_builder_get_object (builder, "retry_button"),
		"clicked", G_CALLBACK (retry_load), builder);

	g_signal_connect_swapped (gtk_builder_get_object (builder, "normal_button"),
		"clicked", G_CALLBACK (on_set_mode_normal), builder);
	g_signal_connect_swapped (gtk_builder_get_object (builder, "legacy_button"),
		"clicked", G_CALLBACK (on_set_mode_legacy), builder);
	g_signal_connect_swapped (gtk_builder_get_object (builder, "apply_button"),
		"clicked", G_CALLBACK (save_configuration), builder);

	gtk_main ();
	g_object_unref (builder);
	return 0;
}

