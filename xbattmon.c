/*
 * (c) 2007, Caoimhe Chaos <caoimhechaos@protonmail.com>,
 *	     SyGroup GmbH Reinach. All rights reserved.
 *
 * Redistribution and use in source  and binary forms, with or without
 * modification, are permitted  provided that the following conditions
 * are met:
 *
 * * Redistributions of  source code  must retain the  above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this  list of conditions and the  following disclaimer in
 *   the  documentation  and/or  other  materials  provided  with  the
 *   distribution.
 * * Neither  the  name  of the  SyGroup  GmbH  nor  the name  of  its
 *   contributors may  be used to endorse or  promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS"  AND ANY EXPRESS  OR IMPLIED WARRANTIES  OF MERCHANTABILITY
 * AND FITNESS  FOR A PARTICULAR  PURPOSE ARE DISCLAIMED. IN  NO EVENT
 * SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL,  EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED  TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE,  DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT  LIABILITY,  OR  TORT  (INCLUDING NEGLIGENCE  OR  OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Compile with:
 *  gcc -W -Wall -Os `pkg-config --libs --cflags gtk+-2.0` -o xbattmon \
 *	-g xbattmon.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include <paths.h>
#include <sys/envsys.h>
#include <sys/ioctl.h>

#include <signal.h>

#include <gtk/gtk.h>

static u_int dischargerate_sensor = -1, charge_sensor = -1, designcap;
static int fd = -1;
static GtkWidget *window, *oldlabel = NULL;
static GtkStyle *style;

static void destroy(GtkWidget *widget, gpointer *data)
{
	gtk_main_quit();
}

static void rescan_battery_status(int sig)
{
	u_int currcharge = 0, discharge, timeleft;
	u_int hrs = 0, mins = 0, secs = 0, percent;
	envsys_tre_data_t *dischargerate, *charge;
	GtkWidget *label;
	u_char *str;
	u_int len;

	dischargerate = malloc(sizeof(envsys_tre_data_t));
	if (!dischargerate)
	{
		perror("malloc");
		alarm(1);
		return;
	}

	charge = malloc(sizeof(envsys_tre_data_t));
	if (!charge)
	{
		perror("malloc");
		free(dischargerate);
		alarm(1);
		return;
	}

	dischargerate->sensor = dischargerate_sensor;
	charge->sensor = charge_sensor;

	if (ioctl(fd, ENVSYS_GTREDATA, dischargerate))
	{
		perror("ENVSYS_GTREDATA");
		free(dischargerate);
		free(charge);
		alarm(1);
		return;
	}
	if (ioctl(fd, ENVSYS_GTREDATA, charge))
	{
		perror("ENVSYS_GTREDATA");
		alarm(1);

		free(dischargerate);
		free(charge);
		return;
	}

	currcharge = charge->cur.data_s / 1000;
	discharge = dischargerate->cur.data_s / 1000;

	if (currcharge)
		percent = (currcharge * 1000) / designcap;
	else
		percent = 0;

	if (dischargerate->validflags & ENVSYS_FCURVALID)
	{
		if (discharge)
			timeleft = (currcharge * 3600) / discharge;
		else
			timeleft = currcharge * 3600;
		hrs = timeleft / 3600;
		mins = (timeleft / 60) % 60;
		secs = timeleft % 60;

		len = snprintf(NULL, 0, "%d:%02d:%02d left\n%d.%d%%", hrs,
			mins, secs, percent / 10, percent % 10);
		str = malloc(len + 1);
		if (!str)
		{
			perror("malloc");
			free(dischargerate);
			free(charge);
			return;
		}
		memset(str, 0, len + 1);
		snprintf(str, len + 1, "%d:%02d:%02d left\n%d.%d%%", hrs,
			mins, secs, percent / 10, percent % 10);
	}
	else
	{
		len = snprintf(NULL, 0, "Charging...\n%d.%d%%",
			percent / 10, percent % 10);
		str = malloc(len + 1);
		if (!str)
		{
			perror("malloc");
			free(dischargerate);
			free(charge);
			return;
		}
		memset(str, 0, len + 1);
		snprintf(str, len + 1, "Charging...\n%d.%d%%", percent / 10,
			percent % 10);
	}


	/* printf("%d.%d%%\n", percent / 10, percent % 10); */

	label = gtk_label_new(str);
	if (oldlabel)
	{
		gtk_container_remove(GTK_CONTAINER(window), oldlabel);
		gtk_widget_destroy(GTK_WIDGET(label));
	}

	gtk_widget_set_style(label, style);

	gtk_container_add(GTK_CONTAINER(window), label);

	gtk_widget_show(label);

	free(str);
	free(dischargerate);
	free(charge);

	oldlabel = label;

	alarm(1);
}

int main(int argc, char **argv)
{
	envsys_basic_info_t *edt = malloc(sizeof(envsys_tre_data_t));
	int valid = 1;

	if (!edt)
	{
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	fd = open(_PATH_SYSMON, O_RDONLY);
	if (fd == 1)
	{
		perror(_PATH_SYSMON);
		free(edt);
		exit(EXIT_FAILURE);
	}

	signal(SIGALRM, rescan_battery_status);

	memset(edt, 0, sizeof(envsys_basic_info_t));

	while (valid)
	{
		if (ioctl(fd, ENVSYS_GTREINFO, edt))
		{
			perror("ENVSYS_GTREINFO");
			exit(EXIT_FAILURE);
		}

		valid = edt->validflags & ENVSYS_FVALID;

		if (valid)
		{
			if (strcmp(edt->desc, "acpibat0 discharge rate") == 0)
				dischargerate_sensor = edt->sensor;
			else if (strcmp(edt->desc, "acpibat0 charge") == 0)
				charge_sensor = edt->sensor;
			else if (strcmp(edt->desc, "acpibat0 design cap") == 0)
			{
				envsys_tre_data_t data;

				memset(&data, 0, sizeof(data));
				data.sensor = edt->sensor;

				if (ioctl(fd, ENVSYS_GTREDATA, &data))
				{
					perror("ENVSYS_GTREINFO");
					exit(EXIT_FAILURE);
				}

				designcap = data.cur.data_s / 1000;
			}
		}
		edt->sensor++;
	}

	gtk_init(&argc, &argv);

	style = gtk_style_new();

	/**
	 * gdk_font_unref(style->font_desc);
	 * style->font_desc = gdk_font_load("fixed");
	 * gdk_font_ref(style->font_desc);
	 */

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "X battery monitor");
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
	gtk_widget_show(window);

	rescan_battery_status(0);

	gtk_main();

	close(fd);
	exit(EXIT_SUCCESS);
}
