/*
 * Copyright (C) 2017  T+A elektroakustik GmbH & Co. KG
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

#include <cppcutter.h>

#include "dbus_handlers.h"
#include "dbus_handlers.hh"

#include "mock_messages.hh"

/*!
 * \addtogroup dbus_handlers_tests Unit tests
 * \ingroup dbus_handlers
 *
 * DBus handlers unit tests.
 */
/*!@{*/

namespace dbus_handlers_tests
{

static MockMessages *mock_messages;

void cut_setup(void)
{
    mock_messages = new MockMessages;
    cppcut_assert_not_null(mock_messages);
    mock_messages->init();
    mock_messages_singleton = mock_messages;
}

void cut_teardown(void)
{
    mock_messages->check();

    mock_messages_singleton = nullptr;

    delete mock_messages;

    mock_messages = nullptr;
}

/*!\test
 * Do nothing.
 */
void test_dummy()
{
    /* TODO: This is a dummy test.
     *       Remove once there is at least one real test. */
}

}

/*!@}*/
