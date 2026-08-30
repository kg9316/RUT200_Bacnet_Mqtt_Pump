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

    <vuci-form v-slot="{ uciData }" config="gk_bacnet_mqtt" editing>
      <vuci-named-section
        v-slot="{ s }"
        :uci-data="uciData"
        :endpoints="[{ endpoint: 'gk_bacnet_mqtt/config/config' }]"
        name="general"
        data-key="gk_bacnet_mqtt"
        :title="$t('Gateway configuration')"
      >
        <vuci-form-item-switch :uci-section="s" :label="$t('Enabled')" name="enabled" />
        <vuci-form-item-select :uci-section="s" :label="$t('BACnet interface')" name="bacnet_interface" :options="interfaces" />
        <vuci-form-item-input :uci-section="s" :label="$t('MQTT host')" name="mqtt_host" maxlength="128" />
        <vuci-form-item-input :uci-section="s" :label="$t('MQTT port')" name="mqtt_port" rules="port" />
        <vuci-form-item-input :uci-section="s" :label="$t('Topic root')" name="topic_root" maxlength="128" />
        <vuci-form-item-input :uci-section="s" :label="$t('Poll interval (ms)')" name="poll_ms" rules="uinteger" />
        <vuci-form-item-input :uci-section="s" :label="$t('Discovery interval (ms)')" name="discovery_ms" rules="uinteger" />
        <vuci-form-item-input :uci-section="s" :label="$t('Maximum publish age (s)')" name="max_age_sec" rules="uinteger" />
      </vuci-named-section>
    </vuci-form>

    <tlt-card :title="$t('Gateway log')">
      <pre style="min-height:180px;max-height:420px;margin:0;padding:12px;overflow:auto;border:1px solid rgba(127,127,127,.25);border-radius:6px;white-space:pre-wrap;word-break:break-word;font-family:monospace;font-size:12px;line-height:1.45;">{{ log || '-' }}</pre>
      <div style="margin-top:14px;">
        <tlt-button @click="loadLog">{{ $t('Refresh log') }}</tlt-button>
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
      timer: null,
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
        { label: this.$t('Topic root'), value: this.status.topicRoot || '-' },
      ];
    },
  },
  mounted() {
    this.loadRuntime();
    this.loadInterfaces();
    this.loadLog();
    this.timer = setInterval(() => this.loadRuntime(), 5000);
  },
  beforeDestroy() {
    if (this.timer) clearInterval(this.timer);
  },
  methods: {
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
    async loadLog() {
      try {
        const response = await this.$axios.get('/api/gk_bacnet_mqtt/status/log');
        const data = this.findPayload(response, ['log']) || {};
        this.log = data.log || '';
      } catch (error) {
        this.log = this.$t('Unable to read gateway log');
      }
    },
  },
};
</script>
