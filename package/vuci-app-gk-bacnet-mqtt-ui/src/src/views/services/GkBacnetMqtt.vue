<template>
  <div class="gk-page">
    <tlt-card :title="$t('Runtime status')">
      <div class="status-grid">
        <div class="status-item">
          <div class="status-label">{{ $t('Service') }}</div>
          <div class="status-value" :class="status.running ? 'ok' : 'bad'">
            {{ status.running ? $t('Running') : $t('Stopped') }}
          </div>
        </div>
        <div class="status-item">
          <div class="status-label">{{ $t('MQTT') }}</div>
          <div class="status-value" :class="status.mqttConnected ? 'ok' : 'bad'">
            {{ status.mqttConnected ? $t('Connected') : $t('Disconnected') }}
          </div>
        </div>
        <div class="status-item">
          <div class="status-label">{{ $t('BACnet devices') }}</div>
          <div class="status-number">{{ status.devices }}</div>
        </div>
        <div class="status-item">
          <div class="status-label">{{ $t('BACnet points') }}</div>
          <div class="status-number">{{ status.points }}</div>
        </div>
      </div>
      <div class="card-actions">
        <tlt-button @click="loadRuntime">{{ $t('Refresh') }}</tlt-button>
      </div>
    </tlt-card>

    <vuci-form v-slot="{ uciData }" config="gk_bacnet_mqtt">
      <vuci-named-section
        v-slot="{ s }"
        :uci-data="uciData"
        :endpoints="[{ endpoint: 'gk_bacnet_mqtt/config/config' }]"
        name="main"
        data-key="gk_bacnet_mqtt"
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
      <pre class="gateway-log">{{ log || '-' }}</pre>
      <div class="card-actions">
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
      },
      log: '',
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
        this.status = Object.assign(
          { running: !!data.running, mqttConnected: false, devices: 0, points: 0 },
          parsed
        );
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

<style scoped>
.gk-page {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.status-grid {
  display: grid;
  grid-template-columns: repeat(4, minmax(150px, 1fr));
  gap: 12px;
}

.status-item {
  min-height: 72px;
  padding: 14px 16px;
  border: 1px solid rgba(127, 127, 127, 0.25);
  border-radius: 6px;
}

.status-label {
  margin-bottom: 7px;
  font-size: 12px;
  opacity: 0.72;
}

.status-value,
.status-number {
  font-size: 18px;
  font-weight: 600;
}

.status-value.ok {
  opacity: 1;
}

.status-value.bad {
  opacity: 0.72;
}

.card-actions {
  margin-top: 12px;
}

.gateway-log {
  min-height: 180px;
  max-height: 420px;
  margin: 0;
  padding: 12px;
  overflow: auto;
  border: 1px solid rgba(127, 127, 127, 0.25);
  border-radius: 6px;
  white-space: pre-wrap;
  word-break: break-word;
  font-family: monospace;
  font-size: 12px;
  line-height: 1.45;
}

@media (max-width: 900px) {
  .status-grid {
    grid-template-columns: repeat(2, minmax(140px, 1fr));
  }
}

@media (max-width: 520px) {
  .status-grid {
    grid-template-columns: 1fr;
  }
}
</style>
