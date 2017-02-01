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
