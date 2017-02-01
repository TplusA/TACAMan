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

#include "formats.hh"

static Converter::OutputFormatList all_output_formats;

Converter::OutputFormatList::OutputFormatList()
{
    formats_.emplace_back(OutputFormat("png", "120x120"));
    formats_.emplace_back(OutputFormat("png", "200x200"));
    formats_.emplace_back(OutputFormat("jpg", "400x400"));
}

const Converter::OutputFormatList &Converter::get_output_format_list()
{
    return all_output_formats;
}
