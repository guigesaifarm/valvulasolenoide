/**
 * AgroIrriga Pro - Frontend Application
 * Controle de 10 válvulas solenoides com WebSocket realtime
 */

// Configuração
const API_URL = 'http://localhost:8000/api';
const WS_URL = 'ws://localhost:8000/ws';
const MQTT_WS_URL = 'ws://localhost:9001'; // MQTT over WebSocket

// Estado global
let currentValve = null;
let valveStates = Array(10).fill(false);
let wsConnection = null;
let weatherChart = null;

// Inicialização
document.addEventListener('DOMContentLoaded', () => {
    initApp();
});

async function initApp() {
    renderValves();
    initWebSocket();
    await loadInitialData();
    initWeatherChart();
    startRealtimeUpdates();
}

// Renderiza grid de válvulas
function renderValves() {
    const grid = document.getElementById('valvesGrid');
    grid.innerHTML = '';
    
    for (let i = 1; i <= 10; i++) {
        const valveCard = document.createElement('div');
        valveCard.className = 'valve-card';
        valveCard.id = `valve-${i}`;
        valveCard.onclick = () => openValveModal(i);
        
        valveCard.innerHTML = `
            <div class="valve-header">
                <span class="valve-number">Linha ${i}</span>
                <span class="valve-status" id="valve-status-${i}">Desligada</span>
            </div>
            <div class="valve-icon">
                <i class="fas fa-faucet"></i>
            </div>
            <div class="valve-info">
                <div>Solenóide ${i}</div>
                <div class="valve-timer" id="valve-timer-${i}">--:--</div>
            </div>
        `;
        
        grid.appendChild(valveCard);
    }
}

// WebSocket para realtime
function initWebSocket() {
    try {
        wsConnection = new WebSocket(WS_URL);
        
        wsConnection.onopen = () => {
            console.log('🔌 WebSocket conectado');
            updateConnectionStatus(true);
        };
        
        wsConnection.onmessage = (event) => {
            const data = JSON.parse(event.data);
            handleWebSocketMessage(data);
        };
        
        wsConnection.onclose = () => {
            console.log('🔌 WebSocket desconectado');
            updateConnectionStatus(false);
            // Tenta reconectar em 5 segundos
            setTimeout(initWebSocket, 5000);
        };
        
        wsConnection.onerror = (error) => {
            console.error('WebSocket erro:', error);
        };
        
    } catch (error) {
        console.error('Erro ao conectar WebSocket:', error);
    }
}

function handleWebSocketMessage(data) {
    if (data.type === 'mqtt_update') {
        if (data.topic.includes('status')) {
            updateValvesFromStatus(data.data);
        } else if (data.topic.includes('weather')) {
            updateWeatherDisplay(data.data);
        } else if (data.topic.includes('alert')) {
            showAlert(data.data);
        }
    }
}

// Carrega dados iniciais
async function loadInitialData() {
    try {
        // Status das válvulas
        const valvesResponse = await fetch(`${API_URL}/valves/status`);
        const valvesData = await valvesResponse.json();
        updateValvesFromStatus(valvesData);
        
        // Dados meteorológicos
        const weatherResponse = await fetch(`${API_URL}/weather/current`);
        const weatherData = await weatherResponse.json();
        updateWeatherDisplay(weatherData);
        
    } catch (error) {
        console.error('Erro ao carregar dados:', error);
        showToast('Erro ao conectar com servidor', 'error');
    }
}

// Atualiza display das válvulas
function updateValvesFromStatus(data) {
    const valves = data.valves || data;
    
    valves.forEach(valve => {
        const card = document.getElementById(`valve-${valve.valve}`);
        const statusText = document.getElementById(`valve-status-${valve.valve}`);
        
        if (valve.state) {
            card.classList.add('active');
            statusText.textContent = 'LIGADA';
            valveStates[valve.valve - 1] = true;
        } else {
            card.classList.remove('active');
            statusText.textContent = 'Desligada';
            valveStates[valve.valve - 1] = false;
        }
    });
    
    updateActiveCount();
}

function updateActiveCount() {
    const activeCount = valveStates.filter(v => v).length;
    document.getElementById('activeValvesCount').textContent = activeCount;
}

// Controle de válvulas
function openValveModal(valveNumber) {
    currentValve = valveNumber;
    const isActive = valveStates[valveNumber - 1];
    
    document.getElementById('modalValveNumber').textContent = valveNumber;
    document.getElementById('irrigationTime').value = 30;
    
    const animation = document.getElementById('valveAnimation');
    if (isActive) {
        animation.classList.add('active');
    } else {
        animation.classList.remove('active');
    }
    
    document.getElementById('valveModal').classList.add('active');
}

function adjustTime(minutes) {
    const input = document.getElementById('irrigationTime');
    let value = parseInt(input.value) + minutes;
    value = Math.max(5, Math.min(120, value));
    input.value = value;
}

async function confirmValveAction() {
    if (!currentValve) return;
    
    const isActive = valveStates[currentValve - 1];
    const duration = parseInt(document.getElementById('irrigationTime').value);
    
    try {
        const response = await fetch(`${API_URL}/valves/${currentValve}/control`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                state: !isActive,
                duration_minutes: !isActive ? duration : 0
            })
        });
        
        const result = await response.json();
        
        if (result.success) {
            showToast(
                `Válvula ${currentValve} ${isActive ? 'desligada' : 'ligada'} com sucesso`,
                'success'
            );
            closeModal('valveModal');
            // Atualização virá via WebSocket, mas podemos otimista
            valveStates[currentValve - 1] = !isActive;
            updateValvesFromStatus({
                valves: valveStates.map((state, i) => ({
                    valve: i + 1,
                    state: state
                }))
            });
        }
        
    } catch (error) {
        showToast('Erro ao controlar válvula', 'error');
    }
}

async function turnOffAll() {
    if (!confirm('Deseja realmente desligar TODAS as válvulas?')) return;
    
    try {
        const response = await fetch(`${API_URL}/valves/all-off`, {
            method: 'POST'
        });
        
        const result = await response.json();
        showToast('Todas as válvulas desligadas', 'success');
        
    } catch (error) {
        showToast('Erro ao desligar válvulas', 'error');
    }
}

function emergencyStop() {
    turnOffAll();
    document.getElementById('emergencyTimestamp').textContent = 
        new Date().toLocaleString('pt-BR');
    document.getElementById('emergencyModal').classList.add('active');
}

// Weather functions
function updateWeatherDisplay(data) {
    document.getElementById('currentTemp').textContent = 
        `${data.temperature?.toFixed(1) || '--'}°C`;
    document.getElementById('humidity').textContent = 
        `${data.humidity?.toFixed(0) || '--'}%`;
    document.getElementById('windSpeed').textContent = 
        `${data.wind_speed || '--'} km/h`;
    document.getElementById('pressure').textContent = 
        `${data.pressure?.toFixed(0) || '--'} hPa`;
    document.getElementById('rain1h').textContent = 
        `${data.rain_1h?.toFixed(1) || '0'} mm`;
    document.getElementById('solarRad').textContent = 
        `${data.solar_radiation?.toFixed(0) || '--'} W/m²`;
    document.getElementById('lastUpdate').textContent = 
        new Date(data.timestamp).toLocaleTimeString('pt-BR');
    
    // Atualiza descrição do clima
    const weatherDesc = document.getElementById('weatherDesc');
    if (data.rain_1h > 0) {
        weatherDesc.textContent = 'Chovendo';
    } else if (data.temperature > 30) {
        weatherDesc.textContent = 'Ensolarado e Quente';
    } else {
        weatherDesc.textContent = 'Parcialmente Nublado';
    }
}

function initWeatherChart() {
    const ctx = document.getElementById('weatherChart').getContext('2d');
    
    weatherChart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [{
                label: 'Temperatura (°C)',
                data: [],
                borderColor: '#0ea5e9',
                backgroundColor: 'rgba(14, 165, 233, 0.1)',
                tension: 0.4,
                fill: true
            }, {
                label: 'Umidade (%)',
                data: [],
                borderColor: '#10b981',
                backgroundColor: 'rgba(16, 185, 129, 0.1)',
                tension: 0.4,
                fill: true,
                yAxisID: 'y1'
            }]
        },
        options: {
            responsive: true,
            interaction: {
                mode: 'index',
                intersect: false,
            },
            plugins: {
                legend: {
                    labels: { color: '#94a3b8' }
                }
            },
            scales: {
                x: {
                    grid: { color: 'rgba(255, 255, 255, 0.05)' },
                    ticks: { color: '#64748b' }
                },
                y: {
                    grid: { color: 'rgba(255, 255, 255, 0.05)' },
                    ticks: { color: '#64748b' }
                },
                y1: {
                    position: 'right',
                    grid: { drawOnChartArea: false },
                    ticks: { color: '#64748b' }
                }
            }
        }
    });
    
    // Carrega histórico
    loadWeatherHistory();
}

async function loadWeatherHistory() {
    try {
        const response = await fetch(`${API_URL}/weather/history?hours=24`);
        const data = await response.json();
        
        const labels = data.data.map(d => 
            new Date(d.timestamp).getHours() + ':00'
        );
        const temps = data.data.map(d => d.temperature);
        const humidities = data.data.map(d => d.humidity);
        
        weatherChart.data.labels = labels;
        weatherChart.data.datasets[0].data = temps;
        weatherChart.data.datasets[1].data = humidities;
        weatherChart.update();
        
    } catch (error) {
        console.error('Erro ao carregar histórico:', error);
    }
}

// Utilitários
function closeModal(modalId) {
    document.getElementById(modalId).classList.remove('active');
}

function showToast(message, type = 'info') {
    const container = document.getElementById('toastContainer');
    const toast = document.createElement('div');
    toast.className = `toast ${type}`;
    
    const icons = {
        success: 'check-circle',
        error: 'exclamation-circle',
        warning: 'exclamation-triangle',
        info: 'info-circle'
    };
    
    toast.innerHTML = `
        <i class="fas fa-${icons[type]}"></i>
        <span>${message}</span>
    `;
    
    container.appendChild(toast);
    
    setTimeout(() => {
        toast.style.opacity = '0';
        toast.style.transform = 'translateX(100%)';
        setTimeout(() => toast.remove(), 300);
    }, 5000);
}

function updateConnectionStatus(connected) {
    const status = document.getElementById('connectionStatus');
    const dot = status.querySelector('.status-dot');
    const text = status.querySelector('span:last-child');
    
    if (connected) {
        dot.classList.add('online');
        text.textContent = 'Online';
    } else {
        dot.classList.remove('online');
        text.textContent = 'Offline';
    }
}

function startRealtimeUpdates() {
    // Atualiza timers a cada segundo
    setInterval(() => {
        valveStates.forEach((state, i) => {
            if (state) {
                // Aqui você atualizaria o timer visual
                // baseado no tempo de início real
            }
        });
    }, 1000);
}

// Previne fechar modal ao clicar fora
document.querySelectorAll('.modal').forEach(modal => {
    modal.addEventListener('click', (e) => {
        if (e.target === modal && !modal.classList.contains('emergency-modal')) {
            closeModal(modal.id);
        }
    });
});
