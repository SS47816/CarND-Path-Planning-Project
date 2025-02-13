#ifndef HELPERS_H
#define HELPERS_H

#include <math.h>
#include <string>
#include <vector>
#include "json.hpp"

// for convenience
using nlohmann::json;
using std::string;
using std::vector;

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
//   else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

//
// Helper functions related to waypoints and converting from XY to Frenet
//   or vice versa
//

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Calculate distance between two points
double distance(double x1, double y1, double x2, double y2) {
  return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}

// Calculate closest waypoint to current x, y position
int ClosestWaypoint(double x, double y, const vector<double> &maps_x, 
                    const vector<double> &maps_y) {
  double closestLen = 100000; //large number
  int closestWaypoint = 0;

  for (int i = 0; i < maps_x.size(); ++i) {
    double map_x = maps_x[i];
    double map_y = maps_y[i];
    double dist = distance(x,y,map_x,map_y);
    if (dist < closestLen) {
      closestLen = dist;
      closestWaypoint = i;
    }
  }

  return closestWaypoint;
}

// Returns next waypoint of the closest waypoint
int NextWaypoint(double x, double y, double theta, const vector<double> &maps_x, 
                 const vector<double> &maps_y) {
  int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

  double map_x = maps_x[closestWaypoint];
  double map_y = maps_y[closestWaypoint];

  double heading = atan2((map_y-y),(map_x-x));

  double angle = fabs(theta-heading);
  angle = std::min(2*pi() - angle, angle);

  if (angle > pi()/2) {
    ++closestWaypoint;
    if (closestWaypoint == maps_x.size()) {
      closestWaypoint = 0;
    }
  }

  return closestWaypoint;
}

// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, 
                         const vector<double> &maps_x, 
                         const vector<double> &maps_y) {
  int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

  int prev_wp;
  prev_wp = next_wp-1;
  if (next_wp == 0) {
    prev_wp  = maps_x.size()-1;
  }

  double n_x = maps_x[next_wp]-maps_x[prev_wp];
  double n_y = maps_y[next_wp]-maps_y[prev_wp];
  double x_x = x - maps_x[prev_wp];
  double x_y = y - maps_y[prev_wp];

  // find the projection of x onto n
  double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
  double proj_x = proj_norm*n_x;
  double proj_y = proj_norm*n_y;

  double frenet_d = distance(x_x,x_y,proj_x,proj_y);

  //see if d value is positive or negative by comparing it to a center point
  double center_x = 1000-maps_x[prev_wp];
  double center_y = 2000-maps_y[prev_wp];
  double centerToPos = distance(center_x,center_y,x_x,x_y);
  double centerToRef = distance(center_x,center_y,proj_x,proj_y);

  if (centerToPos <= centerToRef) {
    frenet_d *= -1;
  }

  // calculate s value
  double frenet_s = 0;
  for (int i = 0; i < prev_wp; ++i) {
    frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
  }

  frenet_s += distance(0,0,proj_x,proj_y);

  return {frenet_s,frenet_d};
}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, const vector<double> &maps_s, 
                     const vector<double> &maps_x, 
                     const vector<double> &maps_y) {
  int prev_wp = -1;

  while (s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1))) {
    ++prev_wp;
  }

  int wp2 = (prev_wp+1)%maps_x.size();

  double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),
                         (maps_x[wp2]-maps_x[prev_wp]));
  // the x,y,s along the segment
  double seg_s = (s-maps_s[prev_wp]);

  double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
  double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

  double perp_heading = heading-pi()/2;

  double x = seg_x + d*cos(perp_heading);
  double y = seg_y + d*sin(perp_heading);

  return {x,y};
}

inline double getAbsSpeed(double vx, double vy) {
  return sqrt(vx*vx + vy*vy);
}

// Calculate the neighbouring lane's speed and number of cars
vector<double> getLaneInfo(json lane_sensor_fusion) {
  double sum = 0.0;
  double num_cars = double(lane_sensor_fusion.size());
  vector<double> info;
  if (num_cars > 0)
  {
    for (int i = 0; i < num_cars; i++)
    {
      double vx = lane_sensor_fusion[i][3];
      double vy = lane_sensor_fusion[i][4];
      sum += getAbsSpeed(vx, vy);
    }
    info.push_back(sum/num_cars);
    info.push_back(num_cars);
  }
  else
  {
    info.push_back(25.0); // m/s (50+mph)
    info.push_back(0.0);
  }

  std::cout << "Lane Speed: " << info[0]*2.237 << std::endl;

  return info;
}

// Compare two lane conditions with a cost function
inline bool cmpLaneConditions(vector<double> left_lane, vector<double> right_lane) {
  double left_score = left_lane[0] - right_lane[0] + right_lane[1] - left_lane[1];
  if (left_score >= 0.0)
  {
    std::cout << "Pick Left" << std::endl;
    return true;
  }
  else
  {
    std::cout << "Pick Right" << std::endl;
    return false;
  }
}

// get heading in radians [-PI ~ PI]
inline double getTheta(double vx, double vy, double speed) {
  if (vy >= 0) 
  {
    return acos(vx/speed);
  }
  else 
  {
    return -1*acos(vx/speed);
  }
}

// Predict the other car's motion in the next 3s
bool safeToChangeLane(json lane_sensor_fusion, double car_s, double car_speed, 
                                    const vector<double> &maps_x, 
                                    const vector<double> &maps_y, double delta_t=3.0) {

  const double t = 3.0; // in s
  const double car_length = 4.0; // in m
  const double safety_distance = car_length + 5.0 + abs(22.3 - car_speed)*1.0; // in m

  bool safe_to_change_lane = true;
  
  // check time to collision for each car
  for (int i = 0; i < lane_sensor_fusion.size(); i++)
  {
    double x = lane_sensor_fusion[i][1];
    double y = lane_sensor_fusion[i][2];
    double vx = lane_sensor_fusion[i][3];
    double vy = lane_sensor_fusion[i][4];

    double v = getAbsSpeed(vx, vy);
    double theta = getTheta(vx, vy, v);
    
    vector<double> frenet = getFrenet(x, y, theta, maps_x, maps_y);
    double distance = frenet[0] - car_s; // relative distance in s axis wrt our car
    double relative_speed = car_speed - v; // relative speed along the lane wrt their car
    
    // check if we are too close to other cars
    if (abs(distance) <= safety_distance)
    {
      return false;
    }
    
    double time_to_collision;
    if (distance >= 0)
    {
      time_to_collision = (distance - car_length)/relative_speed;
    }
    else
    {
      time_to_collision = (distance + car_length)/relative_speed;
    }

    std::cout << "Car ID: " << i << std::endl;
    std::cout << "Relative Position: " << distance << std::endl;
    std::cout << "Relative Speed: " << relative_speed << std::endl;
    std::cout << "Time to Collision: " << time_to_collision << std::endl;
    
    // check if we will collide with them
    if (time_to_collision >= 0 && time_to_collision <= t)
    {
      safe_to_change_lane = false;
      std::cout << "Dangerous!" << std::endl;
    }
    else
    {
      std::cout << "Safe" << std::endl;
    }
    std::cout << std::endl;
  }

  std::cout << "~~ Safety Check Done ~~" << std::endl;
  std::cout << std::endl;

  return safe_to_change_lane;
}

#endif  // HELPERS_H