import { REED_SENSORS, MOTION_SENSORS } from './common';
import { dateEquals, formatDuration, html } from './util';

const ENTITY_IDS = [...REED_SENSORS, ...MOTION_SENSORS];

interface Status {
  entity_id: string;
  name: string;
  last_changed: Date;
}

export class StatusCard extends HTMLElement {
  declare hass: any;
  unsub: () => void = () => { };

  t0 = performance.now();
  state = new Map<string, Date>();

  rerenderCallback: number | null = null;

  initialize() {
    const sensor = this.hass.states['sensor.change_history'];
    const changes = sensor.attributes['changes'];

    for (const key in changes) {
      this.state.set(key, new Date(changes[key]));
    }

    this._render();
  }

  connectedCallback() {
    console.log(performance.now() - this.t0, this.hass);

    this.initialize();

    this._render();

    this.hass.connection.subscribeMessage(
      (event: any) => {
        console.log(performance.now() - this.t0, event);

        if ('a' in event) {
          // inital state, we already have through this.hass
        } else if ('c' in event) {
          // TODO this is pretty gross
          setTimeout(() => this._render(), 1);
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

      const statuses: Status[] = [];
      for (const [entity_id, last_changed] of this.state) {
        const entity = this.hass.entities[entity_id];
        const device = this.hass.devices[entity.device_id];
        if (!device) continue;

        statuses.push({
          entity_id,
          last_changed,
          name: device.name_by_user ?? device.name,
        });
      }

      statuses.sort((a, b) => a.last_changed.valueOf() - b.last_changed.valueOf());

      let currentDate = new Date();
      let headerSpacing = false;

      for (const event of [...statuses].reverse()) {
        if (event.last_changed.getDate() != currentDate.getDate()) {
          let header = event.last_changed.toLocaleDateString([], {
            weekday: 'long',
            day: 'numeric',
            month: 'long',
          });

          const yesterday = new Date(now);
          yesterday.setDate(yesterday.getDate() - 1);
          if (dateEquals(event.last_changed, yesterday)) {
            header = `Yesterday · ${header}`;
          }

          const separator = document.createElement('div');
          separator.innerHTML = `${header}`;
          separator.style.fontWeight = 'bold';
          if (headerSpacing) { separator.style.paddingTop = `0.4rem`; headerSpacing = false; }
          root.appendChild(separator);

          currentDate = event.last_changed;
        }

        const row = document.createElement('div');
        row.style.display = 'flex';
        row.style.alignItems = 'center';

        const icon = document.createElement('state-badge') as any;
        icon.hass = this.hass;
        icon.stateObj = this.hass.states[event.entity_id];
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

        const stamp = event.last_changed.toLocaleTimeString([], {
          hourCycle: 'h23',
        });

        const details = document.createElement('span');
        details.innerHTML = `${formatDuration(now.valueOf() - event.last_changed.valueOf())} ago · ${stamp}`;

        details.style.color = `var(--secondary-text-color)`;
        details.style.fontSize = `0.8rem`;
        text.appendChild(details);

        row.appendChild(text);

        root.appendChild(row);

        headerSpacing = true;
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
