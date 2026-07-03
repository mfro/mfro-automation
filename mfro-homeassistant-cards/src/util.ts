export function html(strings: TemplateStringsArray, ...values: any[]) {
  return String.raw({ raw: strings }, ...values);
}

export function plural(number: number, label: string, plural = label + 's') {
  return number == 1 ? `1 ${label}` : `${number} ${plural}`;
}

export function formatDuration(milliseconds: number) {
  if (milliseconds < 100) return plural(Math.floor(milliseconds), `millisecond`);

  var seconds = milliseconds / 1000;
  if (seconds < 120) return plural(Math.floor(seconds), `second`);

  var minutes = seconds / 60;
  if (minutes < 120) return plural(Math.floor(minutes), `minute`);

  var hours = minutes / 60;
  if (hours < 48) return plural(Math.floor(hours), `hour`);

  var days = hours / 24;
  return plural(Math.floor(days), `day`);
}

export function dateEquals(a: Date, b: Date) {
  return a.getFullYear() == b.getFullYear()
    && a.getMonth() == b.getMonth()
    && a.getDate() == b.getDate()
}
