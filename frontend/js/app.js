/**
 * Main Application Controller
 */

import { CONFIG } from './config.js';
import { ChartManager } from './charts.js';
import { WebSocketManager } from './websocket.js';
import { APIClient } from './api.js';
import { SleepPanel } from './components/SleepPanel.js';
import { CognitivePanel } from './components/CognitivePanel.js';
import { OverviewPanel } from './components/OverviewPanel.js';
import { NotificationManager } from './components/Notification.js';

class NightWatchApp {
  constructor() {
    this.charts = new ChartManager();
    this.api = new APIClient();
    this.sleepPanel = new SleepPanel();
    this.cognitivePanel = new CognitivePanel();
    this.overviewPanel = new OverviewPanel();
    this.notifications = new NotificationManager();
    this.wsManager = null;
    this.currentSleepData = null;
    this.currentCognitiveData = null;
    this.refreshInterval = null;
    this.historyData = [];
    this.alertState = { sleep: 'unknown' };
    this.cacheHydrated = false;
    this.csvHeader = [
      'time_s',
      'RMS_H',
      'RMS_B',
      'RMS_L',
      'MOV_H',
      'MOV_B',
      'MOV_L',
      'secEv_H',
      'secEv_B',
      'secEv_L',
      'minute_events_H',
      'minute_events_B',
      'minute_events_L',
      'total_events_min',
      'SleepScore',
      'SoundRMS',
      'LightRaw',
      'TempC',
      'monitoring'
    ];
  }

  /**
   * Initialize application
   */
  async init() {
    this.setupEventListeners();
    this.charts.init();
    this.loadCachedHistory();
    this.setupWebSocket();
    await this.loadInitialData();
    this.startAutoRefresh();
  }

  /**
   * Setup event listeners
   */
  setupEventListeners() {
    // Tab switching
    document.getElementById('tabOverview')?.addEventListener('click', () => this.showPanel('overview'));
    document.getElementById('tabSleep')?.addEventListener('click', () => this.showPanel('sleep'));
    document.getElementById('tabCognitive')?.addEventListener('click', () => this.showPanel('cognitive'));

    // Connect button
    document.getElementById('connectBtn')?.addEventListener('click', () => {
      if (this.wsManager) {
        this.wsManager.connect();
      }
    });

    // Clear button
    document.getElementById('clearBtn')?.addEventListener('click', () => this.handleClear());

    // Detail actions
    document.getElementById('detailExportBtn')?.addEventListener('click', () => this.downloadHistory());
    document.getElementById('detailClearBtn')?.addEventListener('click', () => this.handleClear());

    // Clear log
    document.getElementById('clearLog')?.addEventListener('click', () => {
      const logEl = document.getElementById('log');
      if (logEl) logEl.textContent = '';
    });
  }

  /**
   * Setup WebSocket connection
   */
  setupWebSocket() {
    this.wsManager = new WebSocketManager(
      (msg) => this.handleWebSocketMessage(msg),
      (connected) => {
        this.handleConnectionChange(connected);
        // Show notifications
        if (connected) {
          this.notifications.success('Connected to backend', 2000);
        } else {
          this.notifications.warning('Disconnected from backend', 2000);
        }
      }
    );
  }

  /**
   * Handle WebSocket messages
   */
  handleWebSocketMessage(msg) {
    const logEl = document.getElementById('log');
    
    if (msg.type === 'status') {
      if (logEl) {
        const ts = new Date().toLocaleTimeString();
        logEl.textContent += `[${ts}] ${msg.message}\n`;
        logEl.scrollTop = logEl.scrollHeight;
      }
    } else if (msg.type === 'data') {
      this.currentSleepData = msg.payload;
      this.sleepPanel.update(msg.payload);
      this.overviewPanel.updateSleep(msg.payload);
      this.handleSleepAlert(Number(msg.payload.SleepScore));
      this.charts.addRMSPoint(msg.payload.RMS_H, msg.payload.RMS_B, msg.payload.RMS_L);
      this.charts.addAuxPoint(msg.payload.TempC, msg.payload.LightRaw, msg.payload.SoundRMS);
      this.addHistoryRow(msg.payload);
      
      // Update risk assessment
      if (this.currentCognitiveData) {
        this.overviewPanel.updateRisk(
          msg.payload.SleepScore,
          this.currentCognitiveData.total_score,
          this.currentCognitiveData.alert_level
        );
      }
    } else if (msg.type === 'error') {
      console.error('WebSocket error:', msg.message);
    }
  }

  /**
   * Handle connection status change
   */
  handleConnectionChange(connected) {
    const statusEl = document.getElementById('status');
    const connectBtn = document.getElementById('connectBtn');
    const hintEl = document.getElementById('hint');

    if (connected) {
      if (statusEl) {
        statusEl.className = 'status-badge status-badge--connected';
        statusEl.innerHTML = '<i class="fas fa-circle"></i> Connected';
      }
      if (connectBtn) {
        connectBtn.innerHTML = '<i class="fas fa-unlink"></i> <span>Disconnect</span>';
        connectBtn.classList.add('btn-secondary');
      }
      if (hintEl) {
        hintEl.textContent = 'Connected to backend. Real-time data streaming active.';
      }
    } else {
      if (statusEl) {
        statusEl.className = 'status-badge status-badge--disconnected';
        statusEl.innerHTML = '<i class="fas fa-circle"></i> Disconnected';
      }
      if (connectBtn) {
        connectBtn.innerHTML = '<i class="fas fa-plug"></i> <span>Connect</span>';
        connectBtn.classList.remove('btn-secondary');
      }
      if (hintEl) {
        hintEl.textContent = 'Delirium Detection System — Sleep & Cognitive Monitoring';
      }
    }
  }

  /**
   * Load initial data
   */
  async loadInitialData() {
    try {
      if (this.cacheHydrated) {
        this.charts.reset();
        this.sleepPanel.reset();
        this.overviewPanel.reset();
      }
      // Load sleep history
      const history = await this.api.getHistory();
      this.historyData = Array.isArray(history) ? history.map(row => ({ ...row })) : [];
      if (this.historyData.length > 0) {
        this.historyData.forEach((payload, idx) => {
          this.sleepPanel.update(payload);
          this.charts.addRMSPoint(payload.RMS_H, payload.RMS_B, payload.RMS_L);
          this.charts.addAuxPoint(payload.TempC, payload.LightRaw, payload.SoundRMS);
          if (idx === history.length - 1) {
            this.currentSleepData = payload;
            this.overviewPanel.updateSleep(payload);
            this.handleSleepAlert(Number(payload.SleepScore));
          }
        });
        this.saveHistoryToCache();
      }
      this.cacheHydrated = false;

      // Load cognitive data
      await this.loadCognitiveData();
    } catch (error) {
      console.error('Error loading initial data:', error);
    }
  }

  /**
   * Load cognitive assessment data
   */
  async loadCognitiveData() {
    try {
      this.cognitivePanel.showLoading();

      const [assessments, interactions] = await Promise.all([
        this.api.getAssessments(10),
        this.api.getInteractions(20),
      ]);

      if (assessments && assessments.length > 0) {
        const latest = assessments[assessments.length - 1];
        this.currentCognitiveData = latest;
        this.cognitivePanel.updateAssessment(latest);
        this.overviewPanel.updateCognitive(latest);
        this.cognitivePanel.renderAssessments(assessments);

        // Update risk assessment
        if (this.currentSleepData) {
          this.overviewPanel.updateRisk(
            this.currentSleepData.SleepScore,
            latest.total_score,
            latest.alert_level
          );
        }
      } else {
        this.cognitivePanel.renderAssessments([]);
      }

      if (interactions && interactions.length > 0) {
        this.cognitivePanel.renderInteractions(interactions);
      } else {
        this.cognitivePanel.renderInteractions([]);
      }
    } catch (error) {
      console.error('Error loading cognitive data:', error);
      const assessmentsList = document.getElementById('assessmentsList');
      const interactionsList = document.getElementById('interactionsList');
      if (assessmentsList) {
        assessmentsList.innerHTML = '<div class="empty-state" style="color: var(--danger);">Error loading data</div>';
      }
      if (interactionsList) {
        interactionsList.innerHTML = '<div class="empty-state" style="color: var(--danger);">Error loading data</div>';
      }
    }
  }

  /**
   * Handle clear/reset
   */
  async handleClear() {
    const clearBtn = document.getElementById('clearBtn');
    const original = clearBtn?.innerHTML;
    
    if (clearBtn) {
      clearBtn.disabled = true;
      clearBtn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Clearing...';
    }

    try {
      await this.api.reset();
      this.charts.reset();
      this.sleepPanel.reset();
      this.overviewPanel.reset();
      this.currentSleepData = null;
      this.currentCognitiveData = null;
      this.historyData = [];
      this.clearHistoryCache();
      
      const logEl = document.getElementById('log');
      if (logEl) {
        logEl.textContent = '';
        const ts = new Date().toLocaleTimeString();
        logEl.textContent = `[${ts}] Dashboard cleared. Awaiting new data.\n`;
      }
      
      this.notifications.info('Dashboard cleared', 2000);
    } catch (error) {
      console.error('Error clearing data:', error);
      const logEl = document.getElementById('log');
      if (logEl) {
        const ts = new Date().toLocaleTimeString();
        logEl.textContent += `[${ts}] Reset failed: ${error.message}\n`;
      }
    } finally {
      if (clearBtn) {
        clearBtn.disabled = false;
        clearBtn.innerHTML = original || '<i class="fas fa-trash"></i> Clear';
      }
    }
  }

  /**
   * Show panel
   */
  showPanel(panel) {
    const panels = {
      overview: { panel: document.getElementById('overviewPanel'), tab: document.getElementById('tabOverview') },
      sleep: { panel: document.getElementById('sleepPanel'), tab: document.getElementById('tabSleep') },
      cognitive: { panel: document.getElementById('cognitivePanel'), tab: document.getElementById('tabCognitive') },
    };

    // Hide all panels and remove active class from tabs
    Object.values(panels).forEach(({ panel, tab }) => {
      if (panel) panel.classList.remove('active');
      if (tab) tab.classList.remove('active');
    });

    // Show selected panel
    const selected = panels[panel];
    if (selected) {
      if (selected.panel) selected.panel.classList.add('active');
      if (selected.tab) selected.tab.classList.add('active');

      // Load cognitive data when cognitive panel is shown
      if (panel === 'cognitive') {
        this.loadCognitiveData();
      }
    }
  }

  /**
   * Start auto-refresh
   */
  startAutoRefresh() {
    this.refreshInterval = setInterval(() => {
      this.loadCognitiveData();
    }, CONFIG.REFRESH.COGNITIVE_DATA);
  }

  /**
   * Stop auto-refresh
   */
  stopAutoRefresh() {
    if (this.refreshInterval) {
      clearInterval(this.refreshInterval);
      this.refreshInterval = null;
    }
  }

  /**
   * Append a new row to in-memory history (with optional cap)
   */
  addHistoryRow(payload) {
    const cloned = { ...payload };
    this.historyData.push(cloned);
    const maxRows = 5000;
    if (this.historyData.length > maxRows) {
      this.historyData.splice(0, this.historyData.length - maxRows);
    }
    this.saveHistoryToCache();
  }

  /**
   * Download history as CSV
   */
  downloadHistory() {
    if (!this.historyData.length) {
      this.notifications.warning('No sleep data to export yet.', 2500);
      return;
    }

    const lines = [
      this.csvHeader.join(','),
      ...this.historyData.map(row =>
        this.csvHeader
          .map((key) => {
            const value = row[key];
            if (value === undefined || value === null) return '';
            if (typeof value === 'number') return Number(value).toString();
            const str = String(value).replace(/"/g, '""');
            return `"${str}"`;
          })
          .join(',')
      ),
    ];

    const blob = new Blob([lines.join('\n')], { type: 'text/csv' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `sleep_history_${new Date().toISOString().replace(/[:.]/g, '-')}.csv`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
    this.notifications.success('Sleep history exported.', 2500);
  }

  loadCachedHistory() {
    try {
      const cached = localStorage.getItem('sleepHistory');
      if (!cached) return;
      const parsed = JSON.parse(cached);
      if (!Array.isArray(parsed) || !parsed.length) return;
      this.historyData = parsed;
      parsed.forEach((payload, idx) => {
        this.sleepPanel.update(payload);
        this.charts.addRMSPoint(payload.RMS_H, payload.RMS_B, payload.RMS_L);
        this.charts.addAuxPoint(payload.TempC, payload.LightRaw, payload.SoundRMS);
        if (idx === parsed.length - 1) {
          this.currentSleepData = payload;
          this.overviewPanel.updateSleep(payload);
          this.handleSleepAlert(Number(payload.SleepScore));
        }
      });
      this.cacheHydrated = true;
      this.notifications.info('Loaded cached session snapshot.', 2000);
    } catch (error) {
      console.warn('Failed to load cached history', error);
    }
  }

  saveHistoryToCache() {
    try {
      localStorage.setItem('sleepHistory', JSON.stringify(this.historyData));
    } catch (error) {
      console.warn('Unable to persist history cache', error);
    }
  }

  clearHistoryCache() {
    try {
      localStorage.removeItem('sleepHistory');
    } catch (error) {
      console.warn('Unable to clear history cache', error);
    }
  }

  handleSleepAlert(score) {
    if (isNaN(score)) return;
    const thresholds = CONFIG.ALERT_THRESHOLDS || { WARNING: 70, CRITICAL: 50 };
    let bucket = 'ok';
    if (score <= thresholds.CRITICAL) bucket = 'critical';
    else if (score <= thresholds.WARNING) bucket = 'warning';
    if (bucket === this.alertState.sleep) return;
    this.alertState.sleep = bucket;
    if (bucket === 'critical') {
      this.notifications.error('Sleep score critically low — intervene now.', 4000);
    } else if (bucket === 'warning') {
      this.notifications.warning('Sleep quality trending down.', 3000);
    } else {
      this.notifications.success('Sleep score stabilised.', 2000);
    }
  }
}

// Initialize app when DOM is ready
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', () => {
    const app = new NightWatchApp();
    app.init();
    window.nightWatchApp = app; // Expose for debugging
  });
} else {
  const app = new NightWatchApp();
  app.init();
  window.nightWatchApp = app; // Expose for debugging
}

