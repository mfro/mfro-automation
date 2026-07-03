import { LogEvent, REED_SENSORS } from './common';
import { dateEquals, formatDuration, html } from './util';

const ENTITY_IDS = [...REED_SENSORS];

interface Event {
  entity_id: string;
  name: string;
  start: Date;
  duration: number | null; // milliseconds, null indicates ongoing
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

      interface Group {
        start: Date,
        end: Date | null,
        events: Event[],
      }

      const events: Event[] = [];
      for (const [entity_id, state] of this.state) {
        if (!state.history) continue;

        const entity = this.hass.entities[entity_id];
        const device = this.hass.devices[entity.device_id];

        // console.log(entity, device, log);

        let current: null | { start: Date } = null;

        for (const event of state.history) {
          const stamp = new Date(event.last_changed);

          if (event.state == 'off' && current) {
            var duration = stamp.valueOf() - current.start.valueOf();
            // console.log(`${current.start} ${duration / 1000} seconds`);

            events.push({
              entity_id,
              name: device.name_by_user ?? device.name,
              duration,
              start: current.start,
            });

            current = null;
          } else if (event.state == 'on' && !current) {
            current = { start: stamp };
          }
        }

        if (current) {
          events.push({
            entity_id,
            name: device.name_by_user ?? device.name,
            duration: null,
            start: current.start,
          });
        }
      }

      events.sort((a, b) => a.start.valueOf() - b.start.valueOf());

      const groups: Group[] = [];

      let group: Group | null = null;
      for (const event of events) {
        const end = new Date(event.start);
        if (event.duration !== null)
          end.setMilliseconds(end.getMilliseconds() + event.duration);

        if (!group || (group.end !== null && (event.start.valueOf() - group.end.valueOf()) > 1000 * 60 * 15)) {
          groups.push(group = {
            start: event.start,
            end: end,
            events: [],
          });
        }

        group.events.push(event);
        if (event.duration == null) group.end = null;
        else if (group.end !== null && end > group.end) group.end = end;
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

        if (group.end !== null) {
          const header = document.createElement('div');
          header.style.display = 'flex';
          header.style.alignItems = 'baseline';
          header.style.justifyContent = 'space-between';
          if (headerSpacing) { header.style.paddingTop = `0.4rem`; headerSpacing = false; }

          const title = document.createElement('span');

          if (now.valueOf() - group.end.valueOf() < 1000) {
            title.innerHTML = `now`;
          } else {
            const relative = formatDuration(now.valueOf() - group.end.valueOf());
            title.innerHTML = `${relative} ago`;
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
        }

        for (const event of [...group.events].reverse()) {
          const row = document.createElement('div');
          row.style.display = 'flex';
          row.style.alignItems = 'center';

          const icon = document.createElement('state-badge') as any;
          icon.hass = this.hass;
          icon.stateObj = {
            entity_id: event.entity_id,
            state: event.duration === null ? 'on' : 'off',
            attributes: this.hass.states[event.entity_id].attributes,
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
          label.innerHTML = event.name;
          text.appendChild(label);

          const stamp = event.start.toLocaleTimeString([], {
            hourCycle: 'h23',
          });

          const details = document.createElement('span');
          details.innerHTML = event.duration !== null
            ? `${formatDuration(event.duration)} · ${stamp}`
            : `${stamp}`;

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
