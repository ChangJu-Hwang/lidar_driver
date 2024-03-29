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

#include <sick_tim/sick_tim551_2050001_parser.hpp>
#include <sick_tim/sick_tim_common.hpp>
#include <rclcpp/rclcpp.hpp>

namespace sick_tim
{

SickTim5512050001Parser::SickTim5512050001Parser(rclcpp::Node::SharedPtr node)
: AbstractParser(),
  override_range_min_(0.05),
  override_range_max_(10.0),
  override_time_increment_(-1.0)
{
  clock_ = *node->get_clock();
}

SickTim5512050001Parser::~SickTim5512050001Parser()
{
}

int SickTim5512050001Parser::parse_datagram(
  char * datagram, size_t datagram_length, SickTimConfig & config,
  sensor_msgs::msg::LaserScan & msg)
{
  // general message structure:
  //
  // - message header   20 fields
  // - DIST1 header      6 fields
  // - DIST1 data        N fields
  // - RSSI included?    1 field
  // - RSSI1 header      6 fields (optional)
  // - RSSI1 data        N fields (optional)
  // - footer         >= 5 fields, depending on number of spaces in device label
  static const size_t HEADER_FIELDS = 26;
  static const size_t MIN_FOOTER_FIELDS = 5;
  char * cur_field;
  size_t count;

  // Reserve sufficient space
  std::vector<char *> fields;
  fields.reserve(datagram_length / 2);

  // ----- only for debug output
  auto datagram_copy = std::make_unique<char[]>(datagram_length + 1);
  strncpy(datagram_copy.get(), datagram, datagram_length); // datagram will be changed by strtok
  datagram_copy[datagram_length] = 0;

  // ----- tokenize
  count = 0;
  char * saveptr;
  cur_field = strtok_r(datagram, " ", &saveptr);

  while (cur_field != NULL) {
    fields.push_back(cur_field);
    cur_field = strtok_r(NULL, " ", &saveptr);
  }

  count = fields.size();

  // Validate header. Total number of tokens is highly unreliable as this may
  // change when you change the scanning range or the device name using SOPAS ET
  // tool. The header remains stable, however.
  if (count < HEADER_FIELDS + 1 + MIN_FOOTER_FIELDS) {
    RCLCPP_WARN(
      rclcpp::get_logger(
        ""),
      "received less fields than minimum fields (actual: %zu, minimum: %zu), ignoring scan", count,
      HEADER_FIELDS + 1 + MIN_FOOTER_FIELDS);
    RCLCPP_WARN(
      rclcpp::get_logger(
        ""),
      "are you using the correct node? (124 --> sick_tim310_1130000m01, > 32 --> sick_tim551_2050001, 580 --> sick_tim310s01, 592 --> sick_tim310)");
    // ROS_DEBUG("received message was: %s", datagram_copy);
    return ExitError;
  }
  if (strcmp(fields[15], "0")) {
    RCLCPP_WARN(
      rclcpp::get_logger(
        ""), "Field 15 of received data is not equal to 0 (%s). Unexpected data, ignoring scan",
      fields[15]);
    return ExitError;
  }
  if (strcmp(fields[20], "DIST1")) {
    RCLCPP_WARN(
      rclcpp::get_logger(
        ""), "Field 20 of received data is not equal to DIST1i (%s). Unexpected data, ignoring scan",
      fields[20]);
    return ExitError;
  }

  // More in depth checks: check data length and RSSI availability
  // 25: Number of data (<= 10F)
  uint16_t number_of_data = 0;
  sscanf(fields[25], "%hx", &number_of_data);

  if (number_of_data < 1 || number_of_data > 811) {
    RCLCPP_WARN(
      rclcpp::get_logger(
        ""), "Data length is outside acceptable range 1-811 (%d). Ignoring scan", number_of_data);
    return ExitError;
  }
  if (count < HEADER_FIELDS + number_of_data + 1 + MIN_FOOTER_FIELDS) {
    RCLCPP_WARN(
      rclcpp::get_logger(
        ""), "Less fields than expected (expected: >= %zu, actual: %zu). Ignoring scan",
      HEADER_FIELDS + number_of_data + 1 + MIN_FOOTER_FIELDS, count);
    return ExitError;
  }
  RCLCPP_DEBUG(rclcpp::get_logger(""), "Number of data: %d", number_of_data);

  // Calculate offset of field that contains indicator of whether or not RSSI data is included
  size_t rssi_idx = HEADER_FIELDS + number_of_data;
  int tmp;
  sscanf(fields[rssi_idx], "%d", &tmp);
  bool rssi = tmp > 0;
  uint16_t number_of_rssi_data = 0;
  if (rssi) {
    sscanf(fields[rssi_idx + 6], "%hx", &number_of_rssi_data);

    // Number of RSSI data should be equal to number of data
    if (number_of_rssi_data != number_of_data) {
      RCLCPP_WARN(
        rclcpp::get_logger(
          ""), "Number of RSSI data (%d) is not equal to number of range data (%d)", number_of_rssi_data,
        number_of_data);
      return ExitError;
    }

    // Check if the total length is still appropriate.
    // RSSI data size = number of RSSI readings + 6 fields describing the data
    if (count < HEADER_FIELDS + number_of_data + 1 + 6 + number_of_rssi_data + MIN_FOOTER_FIELDS) {
      RCLCPP_WARN(
        rclcpp::get_logger(
          ""), "Less fields than expected with RSSI data (expected: >= %zu, actual: %zu). Ignoring scan",
        HEADER_FIELDS + number_of_data + 1 + 6 + number_of_rssi_data + MIN_FOOTER_FIELDS, count);
      return ExitError;
    }

    if (strcmp(fields[rssi_idx + 1], "RSSI1")) {
      RCLCPP_WARN(
        rclcpp::get_logger(
          ""), "Field %zu of received data is not equal to RSSI1 (%s). Unexpected data, ignoring scan", rssi_idx + 1,
        fields[rssi_idx + 1]);
    }
  }

  // ----- read fields into msg
  msg.header.frame_id = config.frame_id;
  RCLCPP_DEBUG(rclcpp::get_logger(""), "publishing with frame_id %s", config.frame_id.c_str());

  rclcpp::Time start_time = clock_.now(); // will be adjusted in the end

  // <STX> (\x02)
  // 0: Type of command (SN)
  // 1: Command (LMDscandata)
  // 2: Firmware version number (1)
  // 3: Device number (1)
  // 4: Serial number (eg. B96518)
  // 5 + 6: Device Status (0 0 = ok, 0 1 = error)
  // 7: Telegram counter (eg. 99)
  // 8: Scan counter (eg. 9A)
  // 9: Time since startup (eg. 13C8E59)
  // 10: Time of transmission (eg. 13C9CBE)
  // 11 + 12: Input status (0 0)
  // 13 + 14: Output status (8 0)
  // 15: Reserved Byte A (0)

  // 16: Scanning Frequency (5DC)
  uint16_t scanning_freq = -1;
  sscanf(fields[16], "%hx", &scanning_freq);
  msg.scan_time = 1.0 / (scanning_freq / 100.0);
  // ROS_DEBUG("hex: %s, scanning_freq: %d, scan_time: %f", fields[16], scanning_freq, msg.scan_time);

  // 17: Measurement Frequency (36)
  uint16_t measurement_freq = -1;
  sscanf(fields[17], "%hx", &measurement_freq);
  msg.time_increment = 1.0 / (measurement_freq * 100.0);
  if (override_time_increment_ > 0.0) {
    // Some lasers may report incorrect measurement frequency
    msg.time_increment = override_time_increment_;
  }
  // ROS_DEBUG("measurement_freq: %d, time_increment: %f", measurement_freq, msg.time_increment);

  // 18: Number of encoders (0)
  // 19: Number of 16 bit channels (1)
  // 20: Measured data contents (DIST1)

  // 21: Scaling factor (3F800000)
  // ignored for now (is always 1.0):
//      unsigned int scaling_factor_int = -1;
//      sscanf(fields[21], "%x", &scaling_factor_int);
//
//      float scaling_factor = reinterpret_cast<float&>(scaling_factor_int);
//      // ROS_DEBUG("hex: %s, scaling_factor_int: %d, scaling_factor: %f", fields[21], scaling_factor_int, scaling_factor);

  // 22: Scaling offset (00000000) -- always 0
  // 23: Starting angle (FFF92230)
  int32_t starting_angle = -1;
  sscanf(fields[23], "%x", reinterpret_cast<uint32_t *>(&starting_angle));
  msg.angle_min = (starting_angle / 10000.0) / 180.0 * M_PI - M_PI / 2;
  // ROS_DEBUG("starting_angle: %d, angle_min: %f", starting_angle, msg.angle_min);

  // 24: Angular step width (2710)
  uint16_t angular_step_width = -1;
  sscanf(fields[24], "%hx", &angular_step_width);
  msg.angle_increment = (angular_step_width / 10000.0) / 180.0 * M_PI;
  msg.angle_max = msg.angle_min + (number_of_data - 1) * msg.angle_increment;

  // 25: Number of data (<= 10F)
  // This is already determined above in number_of_data

  // adjust angle_min to min_ang config param
  int index_min = 0;
  while (msg.angle_min + msg.angle_increment < config.min_ang) {
    msg.angle_min += msg.angle_increment;
    index_min++;
  }

  // adjust angle_max to max_ang config param
  int index_max = number_of_data - 1;
  while (msg.angle_max - msg.angle_increment > config.max_ang) {
    msg.angle_max -= msg.angle_increment;
    index_max--;
  }

  RCLCPP_DEBUG(rclcpp::get_logger(""), "index_min: %d, index_max: %d", index_min, index_max);
  // ROS_DEBUG("angular_step_width: %d, angle_increment: %f, angle_max: %f", angular_step_width, msg.angle_increment, msg.angle_max);

  // 26..26 + n - 1: Data_1 .. Data_n
  msg.ranges.resize(index_max - index_min + 1);
  for (int j = index_min; j <= index_max; ++j) {
    uint16_t range;
    sscanf(fields[j + HEADER_FIELDS], "%hx", &range);
    if (range == 0) {
      msg.ranges[j - index_min] = std::numeric_limits<float>::infinity();
    } else {
      msg.ranges[j - index_min] = range / 1000.0;
    }
  }

  if (config.intensity) {
    if (rssi) {
      // 26 + n: RSSI data included

      //   26 + n + 1 = RSSI Measured Data Contents (RSSI1)
      //   26 + n + 2 = RSSI scaling factor (3F80000)
      //   26 + n + 3 = RSSI Scaling offset (0000000)
      //   26 + n + 4 = RSSI starting angle (equal to Range starting angle)
      //   26 + n + 5 = RSSI angular step width (equal to Range angular step width)
      //   26 + n + 6 = RSSI number of data (equal to Range number of data)
      //   26 + n + 7 .. 26 + n + 7 + n - 1: RSSI_Data_1 .. RSSI_Data_n
      //   26 + n + 7 + n = unknown (seems to be always 0)
      //   26 + n + 7 + n + 1 = device label included? (0 = no, 1 = yes)
      //   26 + n + 7 + n + 2 .. count - 4 = device label as a length-prefixed string, e.g. 0xA "Scipio_LRF" or 0xB "not defined"
      //   count - 3 .. count - 1 = unknown (but seems to be 0 always)
      //   <ETX> (\x03)
      msg.intensities.resize(index_max - index_min + 1);
      size_t offset = HEADER_FIELDS + number_of_data + 7;
      for (int j = index_min; j <= index_max; ++j) {
        uint16_t intensity;
        sscanf(fields[j + offset], "%hx", &intensity);
        msg.intensities[j - index_min] = intensity;
      }
    } else {
      RCLCPP_WARN_ONCE(
        rclcpp::get_logger(
          ""), "Intensity parameter is enabled, but the scanner is not configured to send RSSI values! "
        "Please read the section 'Enabling intensity (RSSI) output' here: http://wiki.ros.org/sick_tim.");
    }
  }

  // 26 + n: RSSI data included
  // IF RSSI not included:
  //   26 + n + 1 .. 26 + n + 3 = unknown (but seems to be [0, 1, B] always)
  //   26 + n + 4 .. count - 4 = device label
  //   count - 3 .. count - 1 = unknown (but seems to be 0 always)
  //   <ETX> (\x03)

  msg.range_min = override_range_min_;
  msg.range_max = override_range_max_;

  // ----- adjust start time
  // - last scan point = now  ==>  first scan point = now - number_of_data * time increment
  double start_time_adjusted = start_time.seconds() -
    number_of_data * msg.time_increment +           // shift backward to time of first scan point
    index_min * msg.time_increment +                // shift forward to time of first published scan point
    config.time_offset;                             // add time offset (usually negative) to account for USB latency etc.
  if (start_time_adjusted >= 0.0) { // ensure that ros::Time is not negative (otherwise runtime error)
    msg.header.stamp.sec = std::floor(start_time_adjusted);
    msg.header.stamp.nanosec = (start_time_adjusted - std::floor(start_time_adjusted)) * 1e9;
  } else {
    RCLCPP_WARN(
      rclcpp::get_logger(
        ""), "ROS time is 0! Did you set the parameter use_sim_time to true?");
  }

  // ----- consistency check
  float expected_time_increment = msg.scan_time * msg.angle_increment / (2.0 * M_PI);
  if (fabs(expected_time_increment - msg.time_increment) > 0.00001) {
    RCLCPP_WARN_THROTTLE(
      rclcpp::get_logger(
        ""), clock_, 60000, "The time_increment, scan_time and angle_increment values reported by the scanner are inconsistent! "
      "Expected time_increment: %.9f, reported time_increment: %.9f. "
      "Perhaps you should set the parameter time_increment to the expected value. This message will print every 60 seconds.",
      expected_time_increment, msg.time_increment);
  }

  return ExitSuccess;
}

void SickTim5512050001Parser::set_range_min(float min)
{
  override_range_min_ = min;
}

void SickTim5512050001Parser::set_range_max(float max)
{
  override_range_max_ = max;
}

void SickTim5512050001Parser::set_time_increment(float time)
{
  override_time_increment_ = time;
}

}  // namespace sick_tim
