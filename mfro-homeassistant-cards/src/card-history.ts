import { LogEvent, REED_SENSORS } from './common';
import { dateEquals, formatDuration, html, plural } from './util';

const ENTITY_IDS = [...REED_SENSORS];

type Event =
  {
    entity_id: string;
    start: Date;
  }
  & EventChange;

type EventChange =
  | { type: 'open' }
  | { type: 'close', duration: number }
  | { type: 'complete', duration: number }

interface Group {
  start: Date,
  end: Date,
  events: Event[],
}

namespace Event {
  export function end(e: Event) {
    const end = new Date(e.start);
    if (e.type == 'complete')
      end.setMilliseconds(end.getMilliseconds() + e.duration);

    return end;
  }
}

export class HistoryCard extends HTMLElement {
  declare hass: any;
  unsub: () => void = () => { };

  t0 = performance.now();
  state = new Map<string, { history?: LogEvent[] }>();

  rerenderCallback: number | null = null;

  private getEntityState(entity_id: string) {
    let state = this.state.get(entity_id);
    if (!state) this.state.set(entity_id, state = {});

    return state;
  }

  initialize() {
    var now = new Date();
    var start = new Date(now);
    start.setDate(start.getDate() - 1);
    const url = `history/period/${start.toISOString()}`
      + `?filter_entity_id=${ENTITY_IDS.join(',')}`
      + `&no_attributes=true`
      + `&minimal_response=true`;

    this.hass.callApi('GET', url).then((response: LogEvent[][]) => {
      console.log(performance.now() - this.t0, response);

      for (let i = 0; i < ENTITY_IDS.length; ++i) {
        const entity_id = ENTITY_IDS[i];
        const history = [...response[i]];

        this.getEntityState(entity_id).history = history;
      }

      this._render();
    });

    this._render();
  }

  connectedCallback() {
    console.log(performance.now() - this.t0, this.hass);

    this.initialize();

    this.hass.connection.subscribeMessage(
      (event: any) => {
        console.log(performance.now() - this.t0, event);

        if ('a' in event) {
          // inital state, we already have through this.hass
        } else if ('c' in event) {
          for (const entity_id in event['c']) {
            if ('+' in event['c'][entity_id]) {
              const raw = event['c'][entity_id]['+'];
              const logEvent: LogEvent = {
                state: raw['s'],
                last_changed: new Date(raw['lc'] * 1000).toISOString(),
              };

              this.getEntityState(entity_id).history?.push(logEvent);
            }
          }

          this._render();
        }
      },
      {
        type: 'subscribe_entities',
        entity_ids: ENTITY_IDS,
      }
    ).then((v: any) => this.unsub = v);
  }

  _render = () => {
    if (this.state.size) {
      const now = new Date();

      this.innerHTML = ``;

      const root = document.createElement(`ha-card`);
      root.style.padding = `var(--ha-space-4)`;
      root.style.display = `flex`;
      root.style.flexDirection = `column`;

      const events: Event[] = [];
      for (const [entity_id, state] of this.state) {
        if (!state.history) continue;

        console.log(entity_id);

        let latest: null | Event = null;

        for (const event of state.history) {
          const timestamp = new Date(event.last_changed);

          // console.log(latest, event.state);

          if (event.state == 'on' && !latest) {
            events.push(latest = {
              entity_id,
              start: timestamp,
              type: 'open',
            });
          } else if (latest && event.state == 'off') {
            var duration = timestamp.valueOf() - latest.start.valueOf();

            events.push({
              entity_id,
              start: timestamp,
              type: 'close',
              duration,
            });

            latest = null;
          }
        }
      }

      events.sort((a, b) => a.start.valueOf() - b.start.valueOf());

      for (let i = 0; i + 1 < events.length; ++i) {
        const a = events[i];
        const b = events[i + 1];

        if (a.entity_id == b.entity_id
          && a.type == 'open'
          && b.type == 'close') {

          events.splice(i, 2, {
            entity_id: a.entity_id,
            start: a.start,
            type: 'complete',
            duration: b.start.valueOf() - a.start.valueOf(),
          });
        }
      }

      const groups: Group[] = [];

      let group: Group | null = null;
      for (const event of events) {
        const end = Event.end(event);

        if (!group || (event.start.valueOf() - group.end.valueOf()) > 1000 * 60 * 5) {
          groups.push(group = {
            start: event.start,
            end: end,
            events: [],
          });
        }

        group.events.push(event);
        if (end > group.end) group.end = end;
      }

      let currentDate = new Date();
      let headerSpacing = false;

      for (const group of [...groups].reverse()) {
        if (group.start.getDate() != currentDate.getDate()) {
          let header = group.start.toLocaleDateString([], {
            weekday: 'long',
            day: 'numeric',
            month: 'long',
          });

          const yesterday = new Date(now);
          yesterday.setDate(yesterday.getDate() - 1);
          if (dateEquals(group.start, yesterday)) {
            header = `Yesterday · ${header}`;
          }

          const separator = document.createElement('div');
          separator.innerHTML = `${header}`;
          separator.style.fontWeight = 'bold';
          if (headerSpacing) { separator.style.paddingTop = `0.4rem`; headerSpacing = false; }
          root.appendChild(separator);

          currentDate = group.start;
        }

        const entity_ids = new Set(group.events.map(g => g.entity_id));

        const header = document.createElement('div');
        header.style.display = 'flex';
        header.style.alignItems = 'baseline';
        header.style.justifyContent = 'space-between';
        if (headerSpacing) { header.style.marginTop = `0.6rem`; headerSpacing = false; }

        const title = document.createElement('span');
        const stamp = group.events[0].start.toLocaleTimeString([], {
          hourCycle: 'h23',
          hour: 'numeric',
          minute: '2-digit',
        });

        if (now.valueOf() - group.end.valueOf() < 1000) {
          title.innerHTML = `${stamp} · now`;
        } else {
          const relative = formatDuration(now.valueOf() - group.end.valueOf());
          title.innerHTML = `${stamp} · ${relative} ago`;
        }

        header.appendChild(title);

        if (group.events.length != 1) {
          const details = document.createElement('span');
          details.innerHTML = `${formatDuration(group.end.valueOf() - group.start.valueOf())}`;
          details.style.color = `var(--secondary-text-color)`;
          details.style.fontSize = `0.8rem`;
          header.appendChild(details);
        }

        root.appendChild(header);

        for (const entity_id of [...entity_ids].sort((a, b) => a.localeCompare(b))) {
          const events = group.events.filter(e => e.entity_id == entity_id);
          const last = events.at(-1)!;

          const entity = this.hass.entities[entity_id];
          const device = this.hass.devices[entity.device_id];

          const row = document.createElement('div');
          row.style.display = 'flex';
          row.style.alignItems = 'center';

          const icon = document.createElement('state-badge') as any;
          icon.hass = this.hass;
          icon.stateObj = {
            entity_id: entity.entity_id,
            state: last.type == 'open' ? 'on' : 'off',
            attributes: this.hass.states[entity.entity_id].attributes,
          };
          icon.stateColor = true;
          icon.style.height = '32px';
          row.appendChild(icon);

          const text = document.createElement('div');
          text.style.display = 'flex';
          text.style.flex = '1';
          text.style.justifyContent = 'space-between';
          text.style.alignItems = 'baseline';

          const label = document.createElement('span');
          label.innerHTML = device.name_by_user ?? device.name;
          text.appendChild(label);

          const details = document.createElement('span');
          const count = Math.floor(events.map(e => e.type == 'complete' ? 1 : 0.5).reduce((a, b) => a + b, 0));
          if (count > 0)
            details.innerHTML = `${plural(count, 'time')}`
          details.style.color = `var(--secondary-text-color)`;
          details.style.fontSize = `0.8rem`;
          text.appendChild(details);

          row.appendChild(text);

          root.appendChild(row);

          headerSpacing = true;
        }

      }

      this.appendChild(root);
    } else {
      this.innerHTML = html`
        <ha-card style="padding: var(--ha-space-4);">
          ...
        </ha-card>
      `;
    }

    if (this.rerenderCallback !== null) {
      clearTimeout(this.rerenderCallback);
    }

    this.rerenderCallback = setTimeout(() => this._render(), 30000);
  };

  disconnectedCallback() {
    this.unsub();

    if (this.rerenderCallback !== null) {
      clearTimeout(this.rerenderCallback);
    }
  }

  setConfig(config: unknown) { }
}
