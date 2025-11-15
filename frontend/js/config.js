/**
 * Configuration constants for NightWatch Dashboard
 */

export const CONFIG = {
  // API endpoints
  API: {
    BASE_URL: window.location.origin,
    HISTORY: '/api/history',
    LATEST: '/api/latest',
    STATUS: '/api/status',
    SUMMARY: '/api/summary',
    SYSTEM_STATUS: '/api/system-status',
    RESET: '/api/reset',
    ASSESSMENTS: '/api/assessments',
    INTERACTIONS: '/api/interactions',
    WEBSOCKET: '/stream',
  },

  // Chart settings
  CHART: {
    MAX_POINTS: 300,
    UPDATE_INTERVAL: 100, // ms
    ANIMATION_DURATION: 0,
  },

  // Data refresh intervals
  REFRESH: {
    COGNITIVE_DATA: 30000, // 30 seconds
    HISTORY: 60000, // 60 seconds
  },

  // Activity levels
  ACTIVITY: {
    IDLE: 0,
    SLIGHT: 1,
    ATTENTION: 2,
    LABELS: ['Idle', 'Slight', 'Attention'],
  },

  // Alert levels
  ALERT: {
    GREEN: 0,
    YELLOW: 1,
    ORANGE: 2,
    RED: 3,
    LABELS: ['No Risk', 'Mild', 'Moderate', 'Severe'],
  },

  // Sleep score thresholds
  SLEEP_SCORE: {
    EXCELLENT: 85,
    GOOD: 70,
    FAIR: 50,
  },

  // Cognitive score thresholds
  COGNITIVE_SCORE: {
    EXCELLENT: 10,
    GOOD: 7,
    FAIR: 4,
  },

  ALERT_THRESHOLDS: {
    WARNING: 70,
    CRITICAL: 50,
  },

};

