import { html } from 'lit';

export function successRenderer(role, onSelectOther, version) {
  const phone = role === 'phone';
  const board = phone ? 'A' : 'B';
  const other = phone ? 'B' : 'A';
  const guide = 'https://github.com/thomasgregg/DashBridge/blob/main/docs/SETUP.md';

  return ({ close, showLogs }) => [
    `Board ${board} installed`,
    html`
      <div slot="content" style="max-width:420px;line-height:1.6">
        <p style="margin:0 0 18px">Firmware ${version} installed.</p>
        <ol style="padding-left:22px;margin:0 0 18px">
          <li>Label this board <strong>${board} — ${phone ? 'iPhone' : 'Tesla'}</strong>.</li>
          <li>Close this window before unplugging the board.</li>
          <li>${phone
            ? 'Next, pair Dash Calls and Dash Messages with your iPhone for calls and notification access.'
            : 'Next, pair Dash Tesla with your Tesla while parked and send the test message.'}</li>
        </ol>
        <a href="./setup.html" style="color:#126856;font-weight:600;text-underline-offset:3px">Choose apps and check connections →</a>
        <p style="font-size:13px;margin:12px 0 0"><a href=${guide} target="_blank" rel="noopener noreferrer">Detailed pairing and wiring guide ↗</a></p>
        <p style="font-size:13px;margin:18px 0 0">To install Board ${other}, unplug this board and connect the other one.</p>
      </div>
      <div slot="actions" style="display:flex;flex-wrap:wrap;gap:4px;--md-sys-color-primary:#126856">
        <ew-text-button @click=${showLogs}>Logs &amp; Console</ew-text-button>
        <ew-text-button @click=${() => {
          close();
          onSelectOther(phone ? 'car' : 'phone');
        }}>Select Board ${other}</ew-text-button>
        <ew-text-button @click=${close}>Done</ew-text-button>
      </div>
    `,
    true,
  ];
}
