<template>
  <div>
    <tlt-card :title="$t('Runtime status')">
      <div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px">
        <div><strong>{{ $t('Service') }}</strong><br>{{ status.running ? $t('Running') : $t('Stopped') }}</div>
        <div><strong>{{ $t('MQTT') }}</strong><br>{{ status.mqttConnected ? $t('Connected') : $t('Disconnected') }}</div>
        <div><strong>{{ $t('BACnet devices') }}</strong><br>{{ status.devices }}</div>
        <div><strong>{{ $t('BACnet points') }}</strong><br>{{ status.points }}</div>
      </div>
      <div style="margin-top:12px">
        <tlt-button @click="loadRuntime">{{ $t('Refresh') }}</tlt-button>
      </div>
    </tlt-card>

    <vuci-form v-slot="{ uciData }" config="gk-bacnet-mqtt">
      <vuci-named-section
        v-slot="{ s }"
        :uci-data="uciData"
        :endpoints="[{ endpoint: 'gk_bacnet_mqtt/config/config' }]"
        name="main"
        data-key="gk-bacnet-mqtt"
        :title="$t('Gateway configuration')"
      >
        <vuci-form-item-switch :uci-section="s" :label="$t('Enabled')" name="enabled" />
        <vuci-form-item-input :uci-section="s" :label="$t('BACnet interface')" name="bacnet_interface" maxlength="32" />
        <vuci-form-item-input :uci-section="s" :label="$t('MQTT host')" name="mqtt_host" maxlength="128" />
        <vuci-form-item-input :uci-section="s" :label="$t('MQTT port')" name="mqtt_port" rules="port" />
        <vuci-form-item-input :uci-section="s" :label="$t('Topic root')" name="topic_root" maxlength="128" />
        <vuci-form-item-input :uci-section="s" :label="$t('Poll interval (ms)')" name="poll_ms" rules="uinteger" />
        <vuci-form-item-input :uci-section="s" :label="$t('Discovery interval (ms)')" name="discovery_ms" rules="uinteger" />
        <vuci-form-item-input :uci-section="s" :label="$t('Maximum publish age (s)')" name="max_age_sec" rules="uinteger" />
      </vuci-named-section>
    </vuci-form>

    <tlt-card :title="$t('Gateway log')">
      <pre style="max-height:420px;overflow:auto;white-space:pre-wrap">{{ log || '-' }}</pre>
      <tlt-button @click="loadLog">{{ $t('Refresh log') }}</tlt-button>
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
      },
      log: "",
      timer: null,
    };
  },
  mounted() {
    this.loadRuntime();
    this.loadLog();
    this.timer = setInterval(this.loadRuntime, 5000);
  },
  beforeUnmount() {
    if (this.timer) clearInterval(this.timer);
  },
  methods: {
    payload(response) {
      return response && response.data && response.data.data
        ? response.data.data
        : response && response.data
        ? response.data
        : response;
    },
    async loadRuntime() {
      try {
        const response = await this.$axios.get('/api/gk_bacnet_mqtt/status/status');
        const data = this.payload(response) || {};
        const raw = data.status || '{"running":false}';
        const parsed = typeof raw === 'string' ? JSON.parse(raw) : raw;
        this.status = Object.assign({ running: !!data.running, mqttConnected: false, devices: 0, points: 0 }, parsed);
      } catch {
        this.status = { running: false, mqttConnected: false, devices: 0, points: 0 };
      }
    },
    async loadLog() {
      try {
        const response = await this.$axios.get('/api/gk_bacnet_mqtt/status/log');
        const data = this.payload(response) || {};
        this.log = data.log || '';
      } catch {
        this.log = this.$t('Unable to read gateway log');
      }
    },
  },
};
</script>
