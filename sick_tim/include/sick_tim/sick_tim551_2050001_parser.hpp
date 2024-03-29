/*
 * Copyright (C) 2013, Osnabrück University
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Osnabrück University nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *  Created on: 14.11.2013
 *
 *      Author: Martin Günther <mguenthe@uos.de>
 *
 */

#ifndef SICK_TIM__SICK_TIM551_2050001_PARSER_HPP_
#define SICK_TIM__SICK_TIM551_2050001_PARSER_HPP_

#include "abstract_parser.hpp"
#include "sick_tim/sick_tim_common.hpp"

namespace sick_tim
{

class SickTim5512050001Parser : public AbstractParser
{
public:
  explicit SickTim5512050001Parser(rclcpp::Node::SharedPtr node);
  virtual ~SickTim5512050001Parser();

  virtual int parse_datagram(
    char * datagram, size_t datagram_length, SickTimConfig & config,
    sensor_msgs::msg::LaserScan & msg);

  void set_range_min(float min);
  void set_range_max(float max);
  void set_time_increment(float time);

private:
  float override_range_min_, override_range_max_;
  float override_time_increment_;
  rclcpp::Clock clock_;
};

}  // namespace sick_tim
#endif  // SICK_TIM__SICK_TIM551_2050001_PARSER_HPP_
