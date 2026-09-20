import { html } from 'lit';

export function successRenderer() {
  return ({ close, showLogs }) => [
    'DashBridge installed',
    html`
      <div slot="content" style="max-width:420px;line-height:1.6">
        <p>Next, pair this board with your Tesla and iPhone.</p>
        <p>Open Logs &amp; Console and type <strong>pair car</strong> or <strong>pair phone</strong> to start pairing. Type <strong>status</strong> to check both connections.</p>
        <a href="https://github.com/thomasgregg/DashBridge/blob/single-board/docs/SINGLE_BOARD.md" target="_blank" rel="noopener noreferrer" style="color:#126856;font-weight:600">Open setup guide ↗</a>
      </div>
      <div slot="actions" style="display:flex;gap:4px;--md-sys-color-primary:#176c59">
        <ew-text-button @click=${showLogs}>Logs &amp; Console</ew-text-button>
        <ew-text-button @click=${close}>Done</ew-text-button>
      </div>
    `,
    true,
  ];
}
