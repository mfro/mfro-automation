export interface LogEvent {
  state: string;
  last_changed: string;
  attributes?: Record<string, string>;
}

export const REED_SENSORS = [
  'binary_sensor.front_door_door',
  'binary_sensor.garage_door_garage_door_2',
  'binary_sensor.balcony_door_door',
  'binary_sensor.reed_sensor_1',
  'binary_sensor.left_window_window',
  'binary_sensor.right_window_window',
  'binary_sensor.reed_sensor_2',
];

export const MOTION_SENSORS = [
  'binary_sensor.living_room_motion_3',
];
