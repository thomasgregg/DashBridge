import { html } from 'lit';

export function successRenderer() {
  return ({ close, showLogs }) => [
    'Test firmware installed',
    html`
      <div slot="content" style="max-width:420px;line-height:1.6">
        <p>Keep your iPhone connected to the Tesla as its priority device. Search for <strong>DashBridge Test</strong> in the Tesla Bluetooth settings.</p>
        <p>If connecting the test device disconnects your iPhone, reconnect the iPhone and save the logs. That does not meet the test requirement.</p><p>Open Logs &amp; Console and type <strong>pair car</strong> to reopen discovery for two minutes.</p>
        <a href="https://github.com/thomasgregg/DashBridge/blob/message-only/docs/MESSAGE_ONLY.md" target="_blank" rel="noopener noreferrer" style="color:#126856;font-weight:600">Open setup guide ↗</a>
      </div>
      <div slot="actions" style="display:flex;gap:4px;--md-sys-color-primary:#176c59">
        <ew-text-button @click=${showLogs}>Logs &amp; Console</ew-text-button>
        <ew-text-button @click=${close}>Done</ew-text-button>
      </div>
    `,
    true,
  ];
}
