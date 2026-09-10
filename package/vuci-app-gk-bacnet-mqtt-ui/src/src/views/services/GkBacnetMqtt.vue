<template>
  <div style="display:flex;flex-direction:column;gap:16px;">
    <tlt-card :title="$t('Runtime status')">
      <div v-for="row in statusRows" :key="row.label"
           style="display:flex;align-items:center;justify-content:space-between;gap:24px;padding:12px 0;border-bottom:1px solid rgba(127,127,127,.15);">
        <span style="opacity:.7;font-size:13px;">{{ row.label }}</span>
        <span style="display:flex;align-items:center;gap:8px;font-weight:600;font-size:14px;text-align:right;">
          <span v-if="row.dot"
                :style="{width:'8px',height:'8px',flex:'0 0 auto',borderRadius:'50%',background: row.dot === 'ok' ? '#2e9b57' : '#c44747'}"></span>
          {{ row.value }}
        </span>
      </div>

      <div style="margin-top:14px;">
        <tlt-button @click="loadRuntime">{{ $t('Refresh') }}</tlt-button>
      </div>
    </tlt-card>

    <tlt-card :title="$t('Gateway configuration')">
      <div v-for="field in configFields" :key="field.key"
           style="display:flex;align-items:center;justify-content:space-between;gap:24px;padding:12px 0;border-bottom:1px solid rgba(127,127,127,.15);">
        <label :for="'gk-cfg-' + field.key" style="opacity:.7;font-size:13px;">{{ field.label }}</label>
        <select v-if="field.type === 'select'" :id="'gk-cfg-' + field.key" v-model="config[field.key]" class="gk-select" style="min-width:200px;padding:8px 10px;border:1px solid #768294;border-radius:6px;background:#fff !important;color:#17212e !important;color-scheme:light;">
          <option style="background:#fff !important;color:#17212e !important;" v-for="opt in field.options" :key="opt" :value="opt">{{ opt }}</option>
        </select>
        <tlt-switch v-else-if="field.type === 'switch'" :id="'gk-cfg-' + field.key"
                    :model-value="config[field.key] === '1'"
                    @update:model-value="config[field.key] = $event ? '1' : '0'" />
        <input v-else :id="'gk-cfg-' + field.key" :type="field.type === 'password' ? 'password' : 'text'" v-model="config[field.key]"
               :autocomplete="field.type === 'password' ? 'new-password' : 'off'"
               style="min-width:200px;padding:8px 10px;border:1px solid rgba(127,127,127,.35);border-radius:6px;background:transparent;color:inherit;" />
      </div>

      <p v-if="config.mqtt_mode === 'gk_cloud'" style="font-size:13px;">
        {{ $t('GK Cloud uses edge-broker.gkcloud.no:8883 with verified TLS and automatic token renewal. Tag GUIDs are saved on this router. MQTT messages contain values only, without name, description or unit.') }}
        {{ config.controller_license_configured ? $t('A license is saved. Leave the license field empty to keep it.') : $t('Enter your controller ID and license.') }}
      </p>
      <p v-else-if="config.mqtt_tls === '1'" style="font-size:13px;">
        {{ $t('Set the broker TLS port (usually 8883). An empty CA path uses the system CA bundle. Client certificate and key are optional paths to PEM files on the router.') }}
      </p>
      <div style="margin-top:14px;display:flex;align-items:center;gap:12px;">
        <tlt-button @click="saveConfig">{{ $t('Save & Apply') }}</tlt-button>
        <span v-if="saveMessage" :style="{color: saveError ? '#c44747' : '#2e9b57', fontSize: '13px'}">{{ saveMessage }}</span>
      </div>
    </tlt-card>

    <tlt-card :title="$t('Point export')">
      <p>{{ $t('Download controller ID and the local GUID mapping with names, units and descriptions.') }}</p>
      <tlt-button :disabled="exportBusy" @click="downloadPoints">{{ $t('Download points JSON') }}</tlt-button>
      <p v-if="exportError" role="alert">{{ exportError }}</p>
    </tlt-card>

    <tlt-card :title="$t('Gateway log')">
      <select v-model="logMode" class="gk-select" style="min-width:200px;padding:8px 10px;border:1px solid #768294;border-radius:6px;background:#fff !important;color:#17212e !important;color-scheme:light;" aria-label="Log filter" @change="loadLog">
        <option style="background:#fff !important;color:#17212e !important;" value="mqtt">{{ $t('MQTT and authentication') }}</option>
        <option style="background:#fff !important;color:#17212e !important;" value="all">{{ $t('All gateway events') }}</option>
      </select>
      <pre style="min-height:180px;max-height:420px;margin:0;padding:12px;overflow:auto;border:1px solid rgba(127,127,127,.25);border-radius:6px;white-space:pre-wrap;word-break:break-word;font-family:monospace;font-size:12px;line-height:1.45;">{{ log || '-' }}</pre>
      <div style="margin-top:14px;display:flex;gap:12px;">
        <tlt-button @click="loadLog">{{ $t('Refresh log') }}</tlt-button>
        <tlt-button @click="clearLog">{{ $t('Clear log') }}</tlt-button>
      </div>
    </tlt-card>
  </div>
</template>

<script>
export default {
  data() {
    return {
      status: {
        running: false,
        mqttConnected: false,
        devices: 0,
        points: 0,
        mqttHost: '',
        mqttPort: 0,
        topicRoot: '',
      },
      interfaces: [],
      log: '',
      logMode: 'mqtt',
      exportBusy: false,
      exportError: '',
      timer: null,
      config: {
        enabled: '0',
        bacnet_interface: '',
        mqtt_host: '',
        mqtt_port: '',
        mqtt_mode: 'generic',
        mqtt_tls: '0',
        mqtt_ca_file: '/etc/ssl/certs/ca-certificates.crt',
        mqtt_cert_file: '',
        mqtt_key_file: '',
        controller_id: '',
        controller_license: '',
        controller_license_configured: false,
        topic_root: '',
        poll_ms: '',
        discovery_ms: '',
        max_age_sec: '',
      },
      saveMessage: '',
      saveError: false,
    };
  },
  computed: {
    statusRows() {
      return [
        { label: this.$t('Service'), value: this.status.running ? this.$t('Running') : this.$t('Stopped'), dot: this.status.running ? 'ok' : 'bad' },
        { label: this.$t('MQTT'), value: this.status.mqttConnected ? this.$t('Connected') : this.$t('Disconnected'), dot: this.status.mqttConnected ? 'ok' : 'bad' },
        { label: this.$t('BACnet devices'), value: this.status.devices },
        { label: this.$t('BACnet points'), value: this.status.points },
        { label: this.$t('MQTT broker'), value: `${this.status.mqttHost || '-'}:${this.status.mqttPort || '-'}` },
        { label: this.$t('MQTT TLS'), value: this.status.mqttTls ? this.$t('Enabled') : this.$t('Disabled') },
        { label: this.$t('MQTT messages sent (QoS 0)'), value: this.status.mqttSent || 0 },
        { label: this.$t('Last MQTT send'), value: this.status.mqttLastSent ? new Date(this.status.mqttLastSent * 1000).toLocaleString() : '-' },
        { label: this.$t('MQTT errors since restart'), value: this.status.mqttErrors || 0 },
        { label: this.$t('MQTT error'), value: this.status.mqttLastError || '-' },
        { label: this.$t('Authentication error'), value: this.status.authLastError || '-' },
        { label: this.$t('Topic root'), value: this.status.topicRoot || '-' },
      ];
    },
    configFields() {
      return [
        { key: 'enabled', label: this.$t('Enabled'), type: 'switch' },
        { key: 'bacnet_interface', label: this.$t('BACnet interface'), type: 'select', options: this.interfaces },
        { key: 'mqtt_mode', label: this.$t('MQTT mode'), type: 'select', options: ['generic', 'gk_cloud'] },
        { key: 'mqtt_host', label: this.$t('MQTT host'), type: 'text' },
        { key: 'mqtt_port', label: this.$t('MQTT port'), type: 'text' },
        { key: 'mqtt_tls', label: this.$t('MQTT TLS'), type: 'switch' },
        { key: 'mqtt_ca_file', label: this.$t('CA certificate file'), type: 'text' },
        { key: 'mqtt_cert_file', label: this.$t('Client certificate file'), type: 'text' },
        { key: 'mqtt_key_file', label: this.$t('Client private key file'), type: 'text' },
        { key: 'controller_id', label: this.$t('Controller ID'), type: 'text' },
        { key: 'controller_license', label: this.$t('Controller license'), type: 'password' },
        { key: 'topic_root', label: this.$t('Topic root'), type: 'text' },
        { key: 'poll_ms', label: this.$t('Poll interval (ms)'), type: 'text' },
        { key: 'discovery_ms', label: this.$t('Discovery interval (ms)'), type: 'text' },
        { key: 'max_age_sec', label: this.$t('Maximum publish age (s)'), type: 'text' },
      ].filter((field) => {
        const cloud = this.config.mqtt_mode === 'gk_cloud';
        if (['controller_id', 'controller_license'].includes(field.key)) return cloud;
        if (['mqtt_host', 'mqtt_port', 'mqtt_tls', 'topic_root'].includes(field.key)) return !cloud;
        if (['mqtt_cert_file', 'mqtt_key_file'].includes(field.key)) return !cloud && this.config.mqtt_tls === '1';
        if (field.key === 'mqtt_ca_file') return cloud || this.config.mqtt_tls === '1';
        return true;
      });
    },
  },
  mounted() {
    this.loadRuntime();
    this.loadInterfaces();
    this.loadConfig();
    this.loadLog();
    this.timer = setInterval(() => { this.loadRuntime(); this.loadLog(); }, 5000);
  },
  beforeDestroy() {
    if (this.timer) clearInterval(this.timer);
  },
  beforeUnmount() {
    if (this.timer) clearInterval(this.timer);
  },
  methods: {
    async downloadPoints() {
      this.exportBusy = true;
      this.exportError = '';
      try {
        const response = await this.$axios.get('/api/gk_bacnet_mqtt/status/points');
        const data = this.findPayload(response, ['controllerId', 'points']);
        if (!data || !Array.isArray(data.points)) throw new Error('Invalid point export');
        const url = URL.createObjectURL(new Blob([JSON.stringify(data, null, 2)], { type: 'application/json;charset=utf-8' }));
        const link = document.createElement('a');
        link.href = url; link.download = 'gk-bacnet-points.json';
        document.body.appendChild(link); link.click(); link.remove();
        setTimeout(() => URL.revokeObjectURL(url), 1000);
      } catch (error) {
        this.exportError = this.$t('Unable to download points. Please try again.');
      } finally { this.exportBusy = false; }
    },
    findPayload(value, keys) {
      if (!value || typeof value !== 'object') return null;
      if (keys.some((key) => Object.prototype.hasOwnProperty.call(value, key))) return value;
      const preferred = ['data', 'http_body', 'body', 'result'];
      for (const key of preferred) {
        if (value[key] && typeof value[key] === 'object') {
          const found = this.findPayload(value[key], keys);
          if (found) return found;
        }
      }
      return null;
    },
    async loadRuntime() {
      try {
        const response = await this.$axios.get('/api/gk_bacnet_mqtt/status/status');
        const data = this.findPayload(response, ['status', 'running']) || {};
        let parsed = {};
        if (data.status) {
          parsed = typeof data.status === 'string' ? JSON.parse(data.status) : data.status;
        }
        this.status = Object.assign({
          running: !!data.running,
          mqttConnected: false,
          devices: 0,
          points: 0,
          mqttHost: '',
          mqttPort: 0,
          topicRoot: '',
        }, parsed || {});
      } catch (error) {
        this.status = {
          running: false,
          mqttConnected: false,
          devices: 0,
          points: 0,
          mqttHost: '',
          mqttPort: 0,
          topicRoot: '',
        };
      }
    },
    async loadInterfaces() {
      try {
        const response = await this.$axios.get('/api/gk_bacnet_mqtt/status/interfaces');
        const data = this.findPayload(response, ['interfaces']) || {};
        this.interfaces = Array.isArray(data.interfaces) ? data.interfaces : [];
      } catch (error) {
        this.interfaces = [];
      }
    },
    async loadConfig() {
      try {
        const response = await this.$axios.get('/api/gk_bacnet_mqtt/config/config');
        const data = this.findPayload(response, ['mqtt_host', 'enabled']) || {};
        this.config = Object.assign({}, this.config, data);
      } catch (error) {
        // keep whatever is currently loaded
      }
    },
    async saveConfig() {
      this.saveMessage = '';
      try {
        const response = await this.$axios.post('/api/gk_bacnet_mqtt/config/config', { data: this.config });
        const data = this.findPayload(response, ['mqtt_host', 'enabled']);
        if (!data) throw new Error('Invalid configuration response');
        this.config = Object.assign({}, this.config, data);
        this.saveError = false;
        this.config.controller_license = '';
        this.saveMessage = this.$t('Configuration has been applied');
      } catch (error) {
        this.saveError = true;
        this.saveMessage = this.$t('Failed to save configuration');
      }
    },
    async loadLog() {
      try {
        const mode = this.logMode;
        const response = await this.$axios.get('/api/gk_bacnet_mqtt/status/' + (mode === 'mqtt' ? 'mqtt_log' : 'log'));
        if (mode !== this.logMode) return;
        const data = this.findPayload(response, ['log']) || {};
        this.log = data.log || '';
      } catch (error) {
        this.log = this.$t('Unable to read gateway log');
      }
    },
    clearLog() {
      // Only clears this view; logread has no per-tag clear, and the
      // underlying system log is shared with every other service.
      this.log = '';
    },
  },
};
</script>

