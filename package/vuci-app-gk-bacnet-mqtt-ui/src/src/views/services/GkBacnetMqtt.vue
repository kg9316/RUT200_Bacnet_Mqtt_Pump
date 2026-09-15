<template>
  <div style="display:flex;flex-direction:column;gap:16px;">
    <tlt-card :title="$t('Runtime status')">
      <div v-for="row in statusRows" :key="row.label"
           style="display:flex;align-items:center;justify-content:space-between;gap:24px;padding:12px 0;border-bottom:1px solid rgba(127,127,127,.15);">
        <span style="opacity:.7;font-size:13px;">{{ row.label }}</span>
        <span style="display:flex;align-items:center;gap:8px;font-weight:600;font-size:14px;text-align:right;">
          <tlt-badge v-if="row.dot" :type="row.dot === 'ok' ? 'primary' : 'inactive'">{{ row.value }}</tlt-badge>
          <span v-else>{{ row.value }}</span>
        </span>
      </div>

      <div style="margin-top:14px;">
        <tlt-button @click="loadRuntime">{{ $t('Refresh') }}</tlt-button>
      </div>
    </tlt-card>

    <tlt-table id="gk-bacnet-devices" :title="$t('BACnet devices')"
               :data-source="deviceRows" :columns="deviceColumns" id-key="id"
               :pagination="true" :initial-per-page="25" @refresh="loadRuntime">
      <template #state="{ record }">
        <tlt-badge :type="record.state === 'online' ? 'primary' : record.state === 'offline' ? 'inactive' : 'disabled'">
          {{ $t(record.state === 'online' ? 'Online' : record.state === 'offline' ? 'Offline' : 'Unknown') }}
        </tlt-badge>
      </template>
    </tlt-table>

    <tlt-card :title="$t('Gateway configuration')">
      <div v-for="field in configFields" :key="field.key"
           style="display:flex;align-items:center;justify-content:space-between;gap:24px;padding:12px 0;border-bottom:1px solid rgba(127,127,127,.15);">
        <label :for="'gk-cfg-' + field.key" style="opacity:.7;font-size:13px;">{{ field.label }}</label>
        <tlt-select v-if="field.type === 'select'" :id="'gk-cfg-' + field.key"
                    v-model="config[field.key]" :data-source="field.options.map(opt => ({ key: opt, value: opt }))"
                    :readonly="false" style="width:200px;" />
        <tlt-switch v-else-if="field.type === 'switch'" :id="'gk-cfg-' + field.key"
                    :model-value="config[field.key] === '1'"
                    @update:model-value="config[field.key] = $event ? '1' : '0'" />
        <input v-else :id="'gk-cfg-' + field.key" :type="field.type === 'password' ? 'password' : field.type === 'number' ? 'number' : 'text'" :min="field.type === 'number' ? 1 : undefined" :max="field.type === 'number' ? 100 : undefined" v-model="config[field.key]"
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
      <p>{{ $t('Restore points from an export or tags.json. Existing points are kept; conflicting GUIDs are rejected.') }}</p>
      <input ref="importFile" type="file" accept=".json,application/json" style="display:none" :disabled="importBusy" @change="chooseImport" />
      <tlt-button :disabled="importBusy" @click="$refs.importFile.click()">{{ $t('Choose points JSON') }}</tlt-button>
      <p v-if="importPreview">{{ importPreview }}</p>
      <tlt-button v-if="importDocument" :disabled="importBusy" @click="importPoints">{{ $t('Import points') }}</tlt-button>
      <p v-if="importMessage" role="status">{{ importMessage }}</p>
    </tlt-card>

    <tlt-card :title="$t('Gateway log')">
      <tlt-select id="gk-log-filter" v-model="logMode" :readonly="false" style="width:260px;"
                  :data-source="[{ key: 'mqtt', value: $t('MQTT and authentication') }, { key: 'all', value: $t('All gateway events') }]"
                  @update:model-value="loadLog" />
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
      importDocument: null,
      importPreview: '',
      importMessage: '',
      importBusy: false,
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
        rpm_batch_max: '30',
        poll_ms: '',
        discovery_ms: '',
        max_age_sec: '',
      },
      saveMessage: '',
      saveError: false,
    };
  },
  computed: {
    deviceRows() {
      return (this.status.deviceDetails || []).map(d => ({ ...d,
        name: d.name || String(d.id),
        state: this.status.running ? d.state : 'unknown',
        pollModeText: this.$t(({multiple:'ReadMultiple', single_configured:'Single read (configured)', single_unsupported:'Single read (RPM unsupported)', single_fallback:'Single read (fallback)', pending:'Not yet confirmed'})[d.pollMode] || 'Not yet confirmed'),
        lastPollText: d.lastPollMode === 'multiple' ? `ReadMultiple (${d.lastPollCount})` : d.lastPollMode === 'single' ? this.$t('Single read (1)') : '-',
        rpmLimitText: d.rpmBatch === 1 ? this.$t('Single read') : d.rpmBatch || '-',
        lastResponseText: d.lastResponse ? new Date(d.lastResponse * 1000).toLocaleString() : '-',
      }));
    },
    deviceColumns() {
      return [
        { dataIndex: 'id', title: this.$t('Device ID') },
        { dataIndex: 'name', title: this.$t('Name') },
        { dataIndex: 'state', title: this.$t('Status') },
        { dataIndex: 'points', title: this.$t('Known points') },
        { dataIndex: 'pollModeText', title: this.$t('Read mode') },
        { dataIndex: 'rpmLimitText', title: this.$t('RPM limit') },
        { dataIndex: 'lastResponseText', title: this.$t('Last response') },
        { dataIndex: 'retryIn', title: this.$t('Retry in seconds') },
      ];
    },
    statusRows() {
      return [
        { label: this.$t('BACnet availability'), value: `${this.deviceRows.filter(d => d.state === 'online').length} ${this.$t('online')} / ${this.deviceRows.filter(d => d.state === 'offline').length} ${this.$t('offline')} / ${this.deviceRows.filter(d => d.state === 'unknown').length} ${this.$t('unknown')}` },
        { label: this.$t('Package version'), value: this.status.packageVersion || '-' },
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
        { key: 'rpm_batch_max', label: this.$t('Maximum points per ReadMultiple (1-100; 1 = single read)'), type: 'number' },
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
    async chooseImport(event) {
      this.importDocument = null; this.importPreview = ''; this.importMessage = '';
      const file = event.target.files && event.target.files[0];
      if (!file) return;
      try {
        if (file.size > 2 * 1024 * 1024) throw new Error('Maximum upload size is 2 MB');
        const doc = JSON.parse(await file.text());
        if (!doc || typeof doc !== 'object' || Array.isArray(doc)) throw new Error('Invalid JSON file');
        const count = Array.isArray(doc.points) ? doc.points.length : Object.keys(doc).filter(k => /^\d+:\d+:\d+$/.test(k)).length;
        if (!count) throw new Error('File contains no points');
        const controller = doc.controllerId || doc.controller_id;
        if (controller && controller !== this.config.controller_id) throw new Error('Controller ID does not match this gateway');
        this.importDocument = doc;
        this.importPreview = `${file.name}: ${count} ${this.$t('points')}. ${this.$t('Existing GUIDs are preserved. Click Import points to apply.')}`;
      } catch (e) { this.importMessage = e.message; }
    },
    async importPoints() {
      if (!this.importDocument || this.importBusy) return;
      this.importBusy = true; this.importMessage = this.$t('Importing...');
      const id = `${Date.now()}-${Math.random().toString(16).slice(2)}`;
      try {
        const response = await this.$axios.post('/api/gk_bacnet_mqtt/config/config', {data:{id,document:this.importDocument}});
        const queued = this.findPayload(response, ['queued']);
        if (!queued || !queued.queued) throw new Error(this.findPayload(response,['error'])?.error || 'Import could not be queued');
        for (let i=0; i<30; i++) {
          await new Promise(resolve => setTimeout(resolve, 1000));
          const response = await this.$axios.get('/api/gk_bacnet_mqtt/status/status');
          const result = this.findPayload(response, ['id']);
          if (!result || result.id !== id) continue;
          if (!result.ok) throw new Error(result.error || 'Import rejected');
          this.importMessage = `${this.$t('Import completed')}: ${result.added} ${this.$t('new points')}, ${result.updated} ${this.$t('existing points')}.`;
          this.importDocument = null; this.importPreview = ''; await this.loadRuntime();return;
        }
        throw new Error('Import result not confirmed. Refresh before trying again.');
      } catch (e) { this.importMessage = e.message; }
      finally { this.importBusy = false; }
    },
    async downloadPoints() {
      this.exportBusy = true;
      this.exportError = '';
      try {
        const response = await this.$axios.get('/api/gk_bacnet_mqtt/status/points');
        const data = this.findPayload(response, ['controllerId', 'points']);
        if (!data || !Array.isArray(data.points)) throw new Error('Invalid point export');
        const url = URL.createObjectURL(new Blob([JSON.stringify(data)], { type: 'application/json;charset=utf-8' }));
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
        const response = await this.$axios.post('/api/gk_bacnet_mqtt/config/config', { data: { ...this.config, rpm_batch_max: String(this.config.rpm_batch_max) } });
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

