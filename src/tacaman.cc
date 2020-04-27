/*
 * Copyright (C) 2017, 2020  T+A elektroakustik GmbH & Co. KG
 *
 * This file is part of TACAMan.
 *
 * TACAMan is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 3 as
 * published by the Free Software Foundation.
 *
 * TACAMan is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with TACAMan.  If not, see <http://www.gnu.org/licenses/>.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cstring>
#include <iostream>

#include <glib-unix.h>

#include "messages.h"
#include "messages_glib.h"
#include "dbus_iface.h"
#include "dbus_handlers.hh"
#include "formats.hh"
#include "os.h"
#include "versioninfo.h"

struct parameters
{
    enum MessageVerboseLevel verbose_level;
    bool run_in_foreground;
    bool connect_to_session_dbus;
    const char *cache_root;
};

ssize_t (*os_read)(int fd, void *dest, size_t count) = read;
ssize_t (*os_write)(int fd, const void *buf, size_t count) = write;

static void show_version_info(void)
{
    printf("%s\n"
           "Revision %s%s\n"
           "         %s+%d, %s\n",
           PACKAGE_STRING,
           VCS_FULL_HASH, VCS_WC_MODIFIED ? " (tainted)" : "",
           VCS_TAG, VCS_TICK, VCS_DATE);
}

static void log_version_info(void)
{
    msg_vinfo(MESSAGE_LEVEL_IMPORTANT, "Rev %s%s, %s+%d, %s",
              VCS_FULL_HASH, VCS_WC_MODIFIED ? " (tainted)" : "",
              VCS_TAG, VCS_TICK, VCS_DATE);
}

static void usage(const char *program_name)
{
    std::cout <<
        "Usage: " << program_name << " [options]\n"
        "\n"
        "Options:\n"
        "  --help         Show this help.\n"
        "  --version      Print version information to stdout.\n"
        "  --verbose lvl  Set verbosity level to given level.\n"
        "  --quiet        Short for \"--verbose quite\".\n"
        "  --fg           Run in foreground, don't run as daemon.\n"
        "  --croot path   Path to cache root.\n"
        "  --session-dbus Connect to session D-Bus.\n"
        "  --system-dbus  Connect to system D-Bus.\n"
        ;
}

static bool check_argument(int argc, char *argv[], int &i)
{
    if(i + 1 >= argc)
    {
        std::cerr << "Option " << argv[i] << " requires an argument.\n";
        return false;
    }

    ++i;

    return true;
}

static int process_command_line(int argc, char *argv[],
                                struct parameters *parameters)
{
    parameters->verbose_level = MESSAGE_LEVEL_NORMAL;
    parameters->run_in_foreground = false;
    parameters->connect_to_session_dbus = true;
    parameters->cache_root = "/var/local/data/tacaman";

    for(int i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "--help") == 0)
            return 1;
        else if(strcmp(argv[i], "--version") == 0)
            return 2;
        else if(strcmp(argv[i], "--fg") == 0)
            parameters->run_in_foreground = true;
        else if(strcmp(argv[i], "--verbose") == 0)
        {
            if(!check_argument(argc, argv, i))
                return -1;

            parameters->verbose_level = msg_verbose_level_name_to_level(argv[i]);

            if(parameters->verbose_level == MESSAGE_LEVEL_IMPOSSIBLE)
            {
                fprintf(stderr,
                        "Invalid verbosity \"%s\". "
                        "Valid verbosity levels are:\n", argv[i]);

                const char *const *names = msg_get_verbose_level_names();

                for(const char *name = *names; name != NULL; name = *++names)
                    fprintf(stderr, "    %s\n", name);

                return -1;
            }
        }
        else if(strcmp(argv[i], "--quiet") == 0)
            parameters->verbose_level = MESSAGE_LEVEL_QUIET;
        else if(strcmp(argv[i], "--croot") == 0)
        {
            if(!check_argument(argc, argv, i))
                return -1;

            parameters->cache_root = argv[i];
        }
        else if(strcmp(argv[i], "--session-dbus") == 0)
            parameters->connect_to_session_dbus = true;
        else if(strcmp(argv[i], "--system-dbus") == 0)
            parameters->connect_to_session_dbus = false;
        else
        {
            std::cerr << "Unknown option \"" << argv[i]
                      << "\". Please try --help.\n";
            return -1;
        }
    }

    return 0;
}

/*!
 * Set up logging, daemonize.
 */
static int setup(const struct parameters *parameters,
                 GMainLoop **loop)
{
    msg_enable_syslog(!parameters->run_in_foreground);
    msg_enable_glib_message_redirection();
    msg_set_verbose_level(parameters->verbose_level);

    if(!parameters->run_in_foreground)
    {
        openlog("tacaman", LOG_PID, LOG_DAEMON);

        if(daemon(0, 0) < 0)
        {
            msg_error(errno, LOG_EMERG, "Failed to run as daemon");
            return -1;
        }
    }

    log_version_info();

    *loop = g_main_loop_new(NULL, FALSE);

    if(*loop == NULL)
        msg_error(ENOMEM, LOG_EMERG, "Failed creating GLib main loop");

    return *loop != NULL ? 0 : -1;
}

static gboolean signal_handler(gpointer user_data)
{
    g_main_loop_quit(static_cast<GMainLoop *>(user_data));
    return G_SOURCE_REMOVE;
}

static void connect_unix_signals(GMainLoop *loop)
{
    g_unix_signal_add(SIGINT, signal_handler, loop);
    g_unix_signal_add(SIGTERM, signal_handler, loop);
}

int main(int argc, char *argv[])
{
    static struct parameters parameters;

    int ret = process_command_line(argc, argv, &parameters);

    if(ret == -1)
        return EXIT_FAILURE;
    else if(ret == 1)
    {
        usage(argv[0]);
        return EXIT_SUCCESS;
    }
    else if(ret == 2)
    {
        show_version_info();
        return EXIT_SUCCESS;
    }

    static GMainLoop *loop = NULL;

    if(setup(&parameters, &loop) < 0)
        return EXIT_FAILURE;

    static constexpr size_t maximum_number_of_keys = 300;
    static const ArtCache::Statistics limits
    (
        maximum_number_of_keys,
        2 * maximum_number_of_keys,
        2 * maximum_number_of_keys * Converter::get_output_format_list().get_formats().size()
    );

    static Converter::Queue converter_queue(parameters.cache_root);
    static ArtCache::Manager cman(parameters.cache_root, limits,
                                  converter_queue);

    converter_queue.init();

    if(!cman.init())
        return EXIT_FAILURE;

    static DBus::SignalData dbus_signal_data(converter_queue, cman);

    if(dbus_setup(loop, parameters.connect_to_session_dbus, &dbus_signal_data) < 0)
        return EXIT_FAILURE;

    connect_unix_signals(loop);
    g_main_loop_run(loop);

    msg_vinfo(MESSAGE_LEVEL_IMPORTANT, "Shutting down");
    converter_queue.shutdown();
    dbus_shutdown(loop);

    return EXIT_SUCCESS;
}
