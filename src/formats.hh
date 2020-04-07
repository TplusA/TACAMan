/*
 * Copyright (C) 2017, 2020  T+A elektroakustik GmbH & Co. KG
 *
 * This file is part of TACAMan.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#ifndef FORMATS_HH
#define FORMATS_HH

#include <vector>
#include <string>

namespace Converter
{

class OutputFormat
{
  public:
    const std::string dimensions_;
    const std::string format_spec_;
    const std::string filename_;

    OutputFormat(const OutputFormat &) = delete;
    OutputFormat(OutputFormat &&) = default;
    OutputFormat &operator=(const OutputFormat &) = delete;

    explicit OutputFormat(const char *format_spec,
                          const char *dimensions):
        dimensions_(dimensions),
        format_spec_(format_spec),
        filename_(format_spec_ + "@" + dimensions_)
    {}

    explicit OutputFormat(const std::string &format_spec,
                          const std::string &dimensions):
        dimensions_(dimensions),
        format_spec_(format_spec),
        filename_(format_spec_ + "@" + dimensions_)
    {}
};

class OutputFormatList
{
  private:
    std::vector<OutputFormat> formats_;

  public:
    OutputFormatList(const OutputFormatList &) = delete;
    OutputFormatList &operator=(const OutputFormatList &) = delete;

    explicit OutputFormatList();

    const std::vector<OutputFormat> &get_formats() const { return formats_; }
};

const OutputFormatList &get_output_format_list();

}

#endif /* !FORMATS_HH */
