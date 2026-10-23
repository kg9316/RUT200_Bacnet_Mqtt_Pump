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

      <template #points="{ record }">

        <tlt-button :disabled="controlBusy || importBusy" @click="openPoints(record.id)">{{ record.enabledPoints == null ? record.points : record.enabledPoints }} / {{ record.points }} — {{ $t('Points') }}</tlt-button>

      </template>

      <template #state="{ record }">

        <tlt-badge :type="record.state === 'online' ? 'primary' : record.state === 'offline' ? 'inactive' : 'disabled'">

          {{ $t(record.state === 'online' ? 'Online' : record.state === 'offline' ? 'Offline' : 'Unknown') }}

        </tlt-badge>

      </template>

    </tlt-table>



    <tlt-card :title="$t('Manual BACnet devices')">

      <p>{{ $t('Add a direct BACnet/IP endpoint. Point discovery starts when the device responds. Offline endpoints are saved and retried.') }}</p>

      <div style="display:flex;gap:12px;flex-wrap:wrap;align-items:center">

        <label>{{ $t('IPv4 address') }} <input v-model="manualIp" placeholder="192.168.1.20" /></label>

        <label>{{ $t('Port') }} <input v-model="manualPort" type="number" min="1" max="65535" /></label>

        <label>{{ $t('Name (optional)') }} <input v-model="manualName" maxlength="95" /></label>

        <label>{{ $t('Device ID (optional)') }} <input v-model="manualId" type="number" min="0" max="4194303" /></label>

        <tlt-button :disabled="controlBusy || importBusy" @click="saveManual">{{ $t('Add / update device') }}</tlt-button>

        <tlt-button :disabled="controlBusy || importBusy" @click="loadManual">{{ $t('Show saved endpoints') }}</tlt-button>

      </div>

      <p>{{ $t('Device ID allows direct reads when I-Am is unavailable. Routed MS/TP devices behind a BACnet router need routed discovery; its IP alone is not a direct device address.') }}</p>

      <div v-for="m in manualTargets" :key="m.ip + ':' + m.p" style="display:flex;gap:12px;align-items:center;margin-top:8px">

        <span>{{ m.ip }}:{{ m.p }} — {{ m.n || '-' }} — {{ m.id == null ? $t('Waiting for Device ID') : m.id }}</span>

        <tlt-button :disabled="controlBusy || importBusy" @click="removeManual(m)">{{ $t('Remove manual endpoint') }}</tlt-button>

      </div>

      <p v-if="controlMessage" role="status">{{ controlMessage }}</p>

    </tlt-card>



    <tlt-card v-if="pointDevice !== null" :title="$t('Point selection') + ' — ' + pointDevice">

      <div style="display:flex;gap:12px;flex-wrap:wrap;align-items:center">

        <label>{{ $t('Search name, description or object ID') }} <input v-model="pointQuery" maxlength="192" @keyup.enter="loadPoints(0)" /></label>

        <tlt-select v-model="pointType" :readonly="false" :data-source="pointTypes" style="width:180px" />

        <tlt-select v-model="pointState" :readonly="false" :data-source="[{key:'all',value:$t('All points')},{key:'enabled',value:$t('Enabled')},{key:'disabled',value:$t('Disabled')}]" style="width:180px" />

        <tlt-button :disabled="controlBusy || importBusy" @click="loadPoints(0)">{{ $t('Search') }}</tlt-button>

      </div>

      <p>{{ pointTotal }} {{ $t('matching points') }} · {{ $t('Page') }} {{ pointPage + 1 }} / {{ Math.max(1, Math.ceil(pointTotal / 100)) }}</p>

      <div style="display:flex;gap:12px;flex-wrap:wrap;align-items:center">

        <tlt-switch :model-value="pointAll" :disabled="controlBusy" @update:model-value="pointAll=$event; pointSelected=[]" />

        <span>{{ $t('Select all matching points, including other pages') }} ({{ pointTotal }})</span>

        <tlt-button :disabled="controlBusy || importBusy" @click="pointSelected=pointRows.map(p => p.key); pointAll=false">{{ $t('Select this page') }}</tlt-button>

        <span>{{ pointAll ? pointTotal : pointSelected.length }} {{ $t('selected') }}</span>

        <tlt-button :disabled="controlBusy || importBusy || (!pointAll && !pointSelected.length)" @click="setPoints(true)">{{ $t('Enable polling') }}</tlt-button>

        <tlt-button :disabled="controlBusy || importBusy || (!pointAll && !pointSelected.length)" @click="setPoints(false)">{{ $t('Disable polling') }}</tlt-button>

      </div>

      <tlt-table id="gk-point-selection" :data-source="pointRows" :columns="pointColumns" id-key="key" :pagination="false">

        <template #selected="{record}"><tlt-switch :disabled="controlBusy || pointAll" :model-value="pointAll || pointSelected.includes(record.key)" @update:model-value="selectPoint(record.key,$event)" /></template>

        <template #enabled="{record}"><tlt-badge :type="record.enabled ? 'primary' : 'inactive'">{{ record.enabled ? $t('Enabled') : $t('Disabled') }}</tlt-badge></template>

      </tlt-table>

      <div style="display:flex;gap:12px;align-items:center">

        <tlt-button :disabled="controlBusy || importBusy || pointPage===0" @click="loadPoints(pointPage-1,true)">{{ $t('Previous') }}</tlt-button>

        <tlt-button :disabled="controlBusy || importBusy || (pointPage+1)*100>=pointTotal" @click="loadPoints(pointPage+1,true)">{{ $t('Next') }}</tlt-button>

        <tlt-switch :disabled="controlBusy || importBusy" :model-value="newPointsEnabled" @update:model-value="setNewPoints($event)" />

        <span>{{ $t('Enable newly discovered points automatically') }}</span>

      </div>

      <p>{{ $t('Disabling polling preserves GUIDs and metadata. New reads and publications stop; an already transmitted request or MQTT message cannot be recalled.') }}</p>

    </tlt-card>



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

        {{ $t('GK Cloud uses edge-broker.gkcloud.no:8883 with verified TLS and automatic token renewal. Tag GUIDs are saved on this router. The first publication includes name, description and unit; subsequent messages contain values.') }}

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

      <p>{{ $t('Restore points from an export or tags.json. GUIDs in the file replace local GUIDs for matching BACnet points. Points not in the file are kept.') }}</p>

      <input ref="importFile" type="file" accept=".json,application/json" style="display:none" :disabled="importBusy || controlBusy" @change="chooseImport" />

      <tlt-button :disabled="importBusy || controlBusy" @click="$refs.importFile.click()">{{ $t('Choose points JSON') }}</tlt-button>

      <p v-if="importPreview">{{ importPreview }}</p>

      <tlt-button v-if="importDocument" :disabled="importBusy || controlBusy" @click="importPoints">{{ $t('Import points') }}</tlt-button>

      <p v-if="importMessage" role="status">{{ importMessage }}</p>

    </tlt-card>



    <tlt-card :title="$t('Gateway log')">

      <tlt-select id="gk-log-filter" v-model="logMode" :readonly="false" style="width:260px;"

                  :data-source="[{ key: 'all', value: $t('All gateway events') }, { key: 'bacnet', value: $t('BACnet') }, { key: 'mqtt', value: $t('MQTT and authentication') }]"

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

      controlBusy: false, controlMessage: '',

      manualIp: '', manualPort: '47808', manualName: '', manualId: '', manualTargets: [],

      pointDevice: null, pointRows: [], pointTotal: 0, pointPage: 0,

      pointQuery: '', pointType: 'all', pointState: 'all', pointFilters: {},

      pointSelected: [], pointAll: false, newPointsEnabled: true,

      interfaces: [],

      log: '',

      logClearing: false,

      logGeneration: 0,

      logMode: 'all',

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

        poll_sec: '',

        discovery_ms: '',

        discovery_min: '',

        max_age_sec: '',

        min_send_sec: '300',

      },

      saveMessage: '',

      saveError: false,

    };

  },

  computed: {

    pointTypes() {

      return [{key:'all',value:this.$t('All types')}, ...[[0,'AI'],[1,'AO'],[2,'AV'],[3,'BI / DI'],[4,'BO / DO'],[5,'BV / DV'],[13,'MI'],[14,'MO'],[19,'MV'],[40,'String value'],[17,'Schedule'],[6,'Calendar']].map(([key,value])=>({key:String(key),value}))];

    },

    pointColumns() {

      return [{dataIndex:'selected',title:this.$t('Select')},{dataIndex:'key',title:this.$t('Object ID')},{dataIndex:'name',title:this.$t('Name')},{dataIndex:'unit',title:this.$t('Unit')},{dataIndex:'description',title:this.$t('Description')},{dataIndex:'enabled',title:this.$t('Polling')}];

    },

    deviceRows() {

      return (this.status.deviceDetails || []).map(d => ({ ...d,

        name: d.name || String(d.id),

        state: this.status.running && this.status.enabled !== false && this.status.bacnetActive !== false ? d.state : 'unknown',

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

      ];

    },

    statusRows() {

      return [

        { label: this.$t('BACnet availability'), value: `${this.deviceRows.filter(d => d.state === 'online').length} ${this.$t('online')} / ${this.deviceRows.filter(d => d.state === 'offline').length} ${this.$t('offline')} / ${this.deviceRows.filter(d => d.state === 'unknown').length} ${this.$t('unknown')}` },

        { label: this.$t('Package version'), value: this.status.packageVersion || '-' },

        { label: this.$t('Service'), value: !this.status.running ? this.$t('Stopped') : this.status.enabled === false ? this.$t('Disabled') : this.$t('Running'), dot: this.status.running && this.status.enabled !== false ? 'ok' : 'bad' },

        { label: this.$t('BACnet'), value: this.$t(({disabled:'Disabled',error:'Error',discovering:'Discovering',running:'Running'})[this.status.bacnetState] || 'Stopped'), dot: this.status.bacnetActive ? 'ok' : 'bad' },

        { label: this.$t('BACnet interface'), value: this.status.bacnetInterface || '-' },

        { label: this.$t('BACnet read requests / replies'), value: `${this.status.bacnetRequests || 0} / ${this.status.bacnetReplies || 0}` },

        { label: this.$t('BACnet timeouts / errors'), value: `${this.status.bacnetTimeouts || 0} / ${this.status.bacnetErrors || 0}` },

        { label: this.$t('Last BACnet reply'), value: this.status.bacnetLastReply ? new Date(this.status.bacnetLastReply * 1000).toLocaleString() : '-' },

        { label: this.$t('BACnet error'), value: this.status.bacnetLastError || '-' },

        { label: this.$t('MQTT'), value: this.status.enabled === false ? this.$t('Disabled') : this.status.mqttConnected ? this.$t('Connected') : this.$t('Disconnected'), dot: this.status.mqttConnected ? 'ok' : 'bad' },

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

        { key: 'poll_sec', label: this.$t('Poll interval (seconds)'), type: 'text' },

        { key: 'discovery_min', label: this.$t('Discovery interval (minutes)'), type: 'text' },

        { key: 'max_age_sec', label: this.$t('Max send time (s, 0 = no periodic sending)'), type: 'text' },

        { key: 'min_send_sec', label: this.$t('Min send time (s, 0 = no rate limit)'), type: 'text' },

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

    async control(action, data={}) {

      if (this.controlBusy || this.importBusy) throw new Error('Another operation is running');

      this.controlBusy=true;

      const id=`${Date.now()}-${Math.random().toString(16).slice(2)}`;

      try {

        const response=await this.$axios.post('/api/gk_bacnet_mqtt/config/config',{data:{...data,action,id}});

        const queued=this.findPayload(response,['queued']);

        if(!queued || !queued.queued) throw new Error(this.findPayload(response,['error'])?.error || 'Operation could not be queued');

        for(let i=0;i<30;i++) {

          await new Promise(resolve=>setTimeout(resolve,1000));

          const reply=await this.$axios.get('/api/gk_bacnet_mqtt/status/status');

          const result=this.findPayload(reply,['id']);

          if(!result || result.id!==id)continue;

          if(!result.ok)throw new Error(result.error || 'Operation rejected');

          return result;

        }

        throw new Error('Result not confirmed. Refresh before retrying.');

      } finally {this.controlBusy=false;}

    },

    async loadManual() {

      try {this.manualTargets=(await this.control('manual_list')).manual || [];}

      catch(e){this.controlMessage=e.message;}

    },

    async saveManual() {

      try {

        const target={ip:this.manualIp.trim(),p:Number(this.manualPort),n:this.manualName.trim()};

        if(this.manualId.trim()!=='')target.id=Number(this.manualId);

        await this.control('manual_save',{target});

        this.controlMessage=this.$t('Endpoint saved. Discovery will retry automatically.');

        await this.loadManual();

      } catch(e){this.controlMessage=e.message;}

    },

    async removeManual(target) {

      try {await this.control('manual_remove',{target});await this.loadManual();this.controlMessage=this.$t('Manual endpoint removed. Existing points and GUIDs are retained.');}

      catch(e){this.controlMessage=e.message;}

    },

    async openPoints(id) {

      if(this.controlBusy || this.importBusy)return;

      this.pointDevice=id;this.pointRows=[];this.pointTotal=0;this.pointQuery='';this.pointType='all';this.pointState='all';

      await this.loadPoints(0);

    },

    async loadPoints(page=0,keepFilter=false) {

      if(this.controlBusy || this.importBusy)return;

      const filter=keepFilter ? this.pointFilters : {q:this.pointQuery,state:this.pointState,...(this.pointType==='all'?{}:{type:Number(this.pointType)})};

      this.pointSelected=[];this.pointAll=false;

      try {

        const result=await this.control('point_list',{device:this.pointDevice,page,...filter});

        this.pointFilters={...filter};this.pointRows=result.rows || [];this.pointTotal=result.total || 0;this.pointPage=page;this.newPointsEnabled=result.newEnabled!==false;

      } catch(e){this.controlMessage=e.message;}

    },

    selectPoint(key,on) {this.pointSelected=on?[...new Set([...this.pointSelected,key])]:this.pointSelected.filter(k=>k!==key);},

    async setPoints(enabled) {

      try {

        const result=await this.control('point_set',{device:this.pointDevice,...this.pointFilters,enabled,all:this.pointAll,keys:this.pointSelected});

        this.controlMessage=`${result.changed} ${this.$t('points updated')}`;

        await this.loadPoints(0,true);await this.loadRuntime();

      } catch(e){this.controlMessage=e.message;}

    },

    async setNewPoints(enabled) {

      try {await this.control('new_points',{device:this.pointDevice,enabled});this.newPointsEnabled=enabled;}

      catch(e){this.controlMessage=e.message;}

    },

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

        this.importPreview = `${file.name}: ${count} ${this.$t('points')}. ${this.$t('GUIDs from this file will become the active GUIDs. Click Import points to apply.')}`;

      } catch (e) { this.importMessage = e.message; }

    },

    async importPoints() {

      if (!this.importDocument || this.importBusy || this.controlBusy) return;

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

        this.applyConfig(data);

      } catch (error) {

        // keep whatever is currently loaded

      }

    },

    applyConfig(data) {

      this.config = Object.assign({}, this.config, data);

      if (data.poll_ms != null) this.config.poll_sec = String(Number(data.poll_ms) / 1000);

      if (data.discovery_ms != null) this.config.discovery_min = String(Number(data.discovery_ms) / 60000);

    },

    async saveConfig() {

      this.saveMessage = '';

      try {

        const { poll_sec, discovery_min, ...config } = this.config;

        const toMilliseconds = (value, multiplier) => {

          const n = Number(String(value).trim().replace(',', '.'));

          if (String(value).trim() === '' || !Number.isFinite(n) || n <= 0) throw new Error('Invalid interval');

          return String(Math.round(n * multiplier));

        };

        const response = await this.$axios.post('/api/gk_bacnet_mqtt/config/config', { data: {

          ...config, rpm_batch_max: String(config.rpm_batch_max),

          poll_ms: toMilliseconds(poll_sec, 1000),

          discovery_ms: toMilliseconds(discovery_min, 60000),

        } });

        const data = this.findPayload(response, ['mqtt_host', 'enabled']);

        if (!data) throw new Error('Invalid configuration response');

        this.applyConfig(data);

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

        const generation = this.logGeneration;

        const mode = this.logMode;

        const response = await this.$axios.get('/api/gk_bacnet_mqtt/status/' + (mode === 'mqtt' ? 'mqtt_log' : 'log'));

        if (mode !== this.logMode || generation !== this.logGeneration || this.logClearing) return;

        const data = this.findPayload(response, ['log']) || {};

        const text = data.log || '';

        this.log = mode === 'bacnet' ? text.split('\n').filter(line => {

          // Match the message, not the process name (which always contains BACnet).

          const message = line.match(/gk-bacnet-mqtt(?:\[\d+\])?:\s*(.*)$/);

          return message && /\bBACnet\b|\bDEVICE\b|\bDEV=|\bRP ACK\b/i.test(message[1]);

        }).join('\n') : text;

      } catch (error) {

        this.log = this.$t('Unable to read gateway log');

      }

    },

    async clearLog() {

      if (this.logClearing) return;

      this.logClearing = true;

      this.logGeneration = (this.logGeneration || 0) + 1;

      try {

        const response = await this.$axios.post('/api/gk_bacnet_mqtt/config/config', {data:{action:'clear_log'}});

        const result = this.findPayload(response, ['cleared']);

        if (!result || !result.cleared) throw new Error('Unable to clear log');

        this.log = '';

      } catch (error) { this.log = this.$t('Unable to clear gateway log. Please try again.'); }

      finally { this.logClearing = false; }

    },

  },

};

</script>



