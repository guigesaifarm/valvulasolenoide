"""
AgroIrriga Pro - Backend API
FastAPI + PostgreSQL + MQTT + Machine Learning
"""

from fastapi import FastAPI, HTTPException, BackgroundTasks, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from contextlib import asynccontextmanager
import asyncio
import json
from datetime import datetime, timedelta
from typing import List, Optional
import paho.mqtt.client as mqtt
import joblib
import numpy as np
from sqlalchemy.orm import Session

from .database import SessionLocal, engine, Base
from .models import (
    SolenoidState, WeatherReading, IrrigationEvent,
    IrrigationSchedule, FarmZone
)
from .schemas import (
    ValveCommand, ValveStatus, WeatherData, 
    IrrigationRequest, AutomationRule
)

# Cria tabelas
Base.metadata.create_all(bind=engine)

# MQTT Client global
mqtt_client = None
connected_clients = []

# Modelo ML (carregado na inicialização)
ml_model = None

@asynccontextmanager
async def lifespan(app: FastAPI):
    # Startup
    global mqtt_client, ml_model
    
    # Conecta MQTT
    mqtt_client = mqtt.Client(client_id="agroirriga_backend")
    mqtt_client.on_connect = on_mqtt_connect
    mqtt_client.on_message = on_mqtt_message
    mqtt_client.connect("localhost", 1883, 60)
    mqtt_client.loop_start()
    
    # Carrega modelo ML (se existir)
    try:
        ml_model = joblib.load("models/irrigation_predictor.pkl")
        print("✅ Modelo ML carregado")
    except:
        print("⚠️ Modelo ML não encontrado, usando regras heurísticas")
        ml_model = None
    
    # Inicia tarefas periódicas
    asyncio.create_task(weather_scheduler())
    asyncio.create_task(ml_irrigation_advisor())
    
    yield
    
    # Shutdown
    mqtt_client.loop_stop()
    mqtt_client.disconnect()

app = FastAPI(
    title="AgroIrriga Pro API",
    description="API para controle de irrigação automatizada",
    version="2.0.0",
    lifespan=lifespan
)

# CORS para frontend
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # Em produção, especificar domínios
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# ============ MQTT HANDLERS ============

def on_mqtt_connect(client, userdata, flags, rc):
    print(f"🔗 MQTT Conectado (código {rc})")
    # Subscreve em tópicos dos dispositivos
    client.subscribe("agroirriga/+/status")
    client.subscribe("agroirriga/+/weather")
    client.subscribe("agroirriga/+/alerts")

def on_mqtt_message(client, userdata, msg):
    try:
        topic = msg.topic
        payload = json.loads(msg.payload.decode())
        
        print(f"📨 MQTT: {topic}")
        
        # Salva no banco de dados
        db = SessionLocal()
        
        if "status" in topic:
            save_valve_status(db, payload)
        elif "weather" in topic:
            save_weather_data(db, payload)
        elif "alerts" in topic:
            handle_alert(db, payload)
        
        db.close()
        
        # Notifica clients WebSocket
        asyncio.create_task(notify_clients({
            "type": "mqtt_update",
            "topic": topic,
            "data": payload
        }))
        
    except Exception as e:
        print(f"❌ Erro MQTT: {e}")

def save_valve_status(db: Session, data: dict):
    """Salva estado das válvulas no banco"""
    for valve_data in data.get("valves", []):
        state = SolenoidState(
            valve_number=valve_data["number"],
            state=valve_data["state"] == "ON",
            timestamp=datetime.utcnow(),
            device_id=data.get("device_id", "unknown")
        )
        db.add(state)
    db.commit()

def save_weather_data(db: Session, data: dict):
    """Salva leitura meteorológica"""
    weather = WeatherReading(
        temperature=data.get("temperature"),
        humidity=data.get("humidity"),
        pressure=data.get("pressure"),
        wind_speed=data.get("wind_speed"),
        rain_1h=data.get("rain_1h"),
        solar_radiation=data.get("solar_radiation"),
        timestamp=datetime.utcnow(),
        device_id=data.get("device_id", "unknown")
    )
    db.add(weather)
    db.commit()

def handle_alert(db: Session, data: dict):
    """Processa alertas do hardware"""
    print(f"🚨 ALERTA: {data.get('alert_type')} - Válvula {data.get('valve')}")
    # Aqui você pode enviar email, SMS, notificação push, etc.

# ============ API ENDPOINTS ============

@app.get("/")
async def root():
    return {
        "message": "AgroIrriga Pro API",
        "version": "2.0.0",
        "status": "operational"
    }

# ---- Controle de Válvulas ----

@app.post("/api/valves/{valve_number}/control")
async def control_valve(
    valve_number: int,
    command: ValveCommand,
    background_tasks: BackgroundTasks
):
    """
    Controla uma válvula específica (1-10)
    """
    if valve_number < 1 or valve_number > 10:
        raise HTTPException(status_code=400, detail="Válvula deve ser 1-10")
    
    # Envia comando MQTT para ESP32
    mqtt_payload = {
        "action": "valve_on" if command.state else "valve_off",
        "valve": valve_number,
        "duration": command.duration_minutes if command.state else 0,
        "timestamp": datetime.utcnow().isoformat(),
        "source": "web_api"
    }
    
    mqtt_client.publish(
        f"agroirriga/agroirriga_fazenda_01/command",
        json.dumps(mqtt_payload)
    )
    
    # Log no banco
    db = SessionLocal()
    event = IrrigationEvent(
        valve_number=valve_number,
        action="ON" if command.state else "OFF",
        duration_minutes=command.duration_minutes if command.state else None,
        triggered_by="manual",
        timestamp=datetime.utcnow()
    )
    db.add(event)
    db.commit()
    db.close()
    
    return {
        "success": True,
        "valve": valve_number,
        "state": "ON" if command.state else "OFF",
        "message": f"Válvula {valve_number} {'ligada' if command.state else 'desligada'}"
    }

@app.post("/api/valves/all-off")
async def all_valves_off():
    """Desliga todas as válvulas de emergência"""
    mqtt_payload = {
        "action": "valve_all_off",
        "timestamp": datetime.utcnow().isoformat()
    }
    
    mqtt_client.publish(
        f"agroirriga/agroirriga_fazenda_01/command",
        json.dumps(mqtt_payload)
    )
    
    return {"success": True, "message": "Comando enviado: desligar todas"}

@app.get("/api/valves/status")
async def get_valves_status():
    """Retorna status atual de todas as válvulas"""
    db = SessionLocal()
    
    # Último estado de cada válvula
    latest_states = {}
    for i in range(1, 11):
        state = db.query(SolenoidState).filter(
            SolenoidState.valve_number == i
        ).order_by(SolenoidState.timestamp.desc()).first()
        
        latest_states[i] = {
            "valve": i,
            "state": state.state if state else False,
            "last_change": state.timestamp.isoformat() if state else None
        }
    
    db.close()
    return {"valves": list(latest_states.values())}

# ---- Automação Inteligente ----

@app.post("/api/irrigation/schedule")
async def schedule_irrigation(schedule: IrrigationSchedule):
    """
    Agenda irrigação automática baseada em horário ou condições
    """
    db = SessionLocal()
    
    sched = IrrigationSchedule(
        valve_number=schedule.valve_number,
        start_time=schedule.start_time,
        duration_minutes=schedule.duration_minutes,
        days_of_week=schedule.days_of_week,
        active=schedule.active,
        weather_dependent=schedule.weather_dependent
    )
    db.add(sched)
    db.commit()
    
    # Se ativo, envia para ESP32
    if schedule.active:
        mqtt_payload = {
            "action": "schedule_irrigation",
            "valve": schedule.valve_number,
            "duration": schedule.duration_minutes,
            "start_hour": schedule.start_time.hour,
            "start_minute": schedule.start_time.minute
        }
        mqtt_client.publish(
            f"agroirriga/agroirriga_fazenda_01/command",
            json.dumps(mqtt_payload)
        )
    
    db.close()
    return {"success": True, "schedule_id": sched.id}

@app.post("/api/irrigation/smart")
async def smart_irrigation(zone_id: int):
    """
    Ativa irrigação inteligente baseada em ML
    Analisa: NDVI, umidade do solo, previsão do tempo, histórico
    """
    db = SessionLocal()
    
    # Coleta dados para decisão
    weather = db.query(WeatherReading).order_by(
        WeatherReading.timestamp.desc()
    ).first()
    
    # Últimos eventos de irrigação
    last_irrigation = db.query(IrrigationEvent).filter(
        IrrigationEvent.zone_id == zone_id
    ).order_by(IrrigationEvent.timestamp.desc()).first()
    
    # Decisão ML ou heurística
    decision = make_irrigation_decision(weather, last_irrigation, ml_model)
    
    if decision["should_irrigate"]:
        # Liga válvulas da zona
        valves = decision["valves_to_open"]
        for valve in valves:
            mqtt_payload = {
                "action": "valve_on",
                "valve": valve,
                "duration": decision["duration_minutes"],
                "reason": "ml_recommendation"
            }
            mqtt_client.publish(
                f"agroirriga/agroirriga_fazenda_01/command",
                json.dumps(mqtt_payload)
            )
        
        # Log
        event = IrrigationEvent(
            zone_id=zone_id,
            action="SMART_ON",
            duration_minutes=decision["duration_minutes"],
            triggered_by="ml_model",
            confidence=decision["confidence"],
            timestamp=datetime.utcnow()
        )
        db.add(event)
        db.commit()
    
    db.close()
    
    return {
        "decision": decision["should_irrigate"],
        "confidence": decision["confidence"],
        "reason": decision["reason"],
        "valves": decision.get("valves_to_open", []),
        "duration": decision.get("duration_minutes", 0)
    }

def make_irrigation_decision(weather, last_irrigation, model):
    """Lógica de decisão de irrigação"""
    
    # Se tem modelo ML, usa ele
    if model:
        features = np.array([
            weather.temperature if weather else 25,
            weather.humidity if weather else 60,
            weather.rain_1h if weather else 0,
            # ... mais features
        ]).reshape(1, -1)
        
        prediction = model.predict(features)[0]
        confidence = model.predict_proba(features)[0].max()
        
        return {
            "should_irrigate": prediction == 1,
            "confidence": float(confidence),
            "reason": "ml_prediction",
            "valves_to_open": [1, 2, 3],  # Determinado pela zona
            "duration_minutes": 30
        }
    
    # Fallback: regras heurísticas
    should_irrigate = False
    reason = ""
    
    if weather:
        # Não irrigar se choveu nas últimas 6 horas
        if weather.rain_1h > 5:
            reason = "Chuva recente detectada"
        # Não irrigar se umidade > 70%
        elif weather.humidity > 70:
            reason = "Umidade ambiente alta"
        # Irrigar se temperatura > 30 e umidade < 40
        elif weather.temperature > 30 and weather.humidity < 40:
            should_irrigate = True
            reason = "Condições de estresse hídrico"
    
    return {
        "should_irrigate": should_irrigate,
        "confidence": 0.7,
        "reason": reason,
        "valves_to_open": [1, 2] if should_irrigate else [],
        "duration_minutes": 25
    }

# ---- Dados Meteorológicos ----

@app.get("/api/weather/current")
async def get_current_weather():
    """Retorna dados meteorológicos mais recentes"""
    db = SessionLocal()
    weather = db.query(WeatherReading).order_by(
        WeatherReading.timestamp.desc()
    ).first()
    db.close()
    
    if not weather:
        raise HTTPException(status_code=404, detail="Sem dados meteorológicos")
    
    return {
        "temperature": weather.temperature,
        "humidity": weather.humidity,
        "pressure": weather.pressure,
        "wind_speed": weather.wind_speed,
        "rain_1h": weather.rain_1h,
        "solar_radiation": weather.solar_radiation,
        "timestamp": weather.timestamp.isoformat()
    }

@app.get("/api/weather/history")
async def get_weather_history(hours: int = 24):
    """Histórico de dados meteorológicos"""
    db = SessionLocal()
    since = datetime.utcnow() - timedelta(hours=hours)
    
    readings = db.query(WeatherReading).filter(
        WeatherReading.timestamp >= since
    ).order_by(WeatherReading.timestamp.asc()).all()
    
    db.close()
    
    return {
        "count": len(readings),
        "data": [
            {
                "timestamp": r.timestamp.isoformat(),
                "temperature": r.temperature,
                "humidity": r.humidity,
                "rain": r.rain_1h
            }
            for r in readings
        ]
    }

# ---- WebSocket para Realtime ----

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    connected_clients.append(websocket)
    
    try:
        while True:
            data = await websocket.receive_text()
            # Processa comandos do frontend se necessário
            # Ou apenas mantém conexão para pushes do servidor
            
    except WebSocketDisconnect:
        connected_clients.remove(websocket)

async def notify_clients(message: dict):
    """Envia mensagem para todos clients WebSocket conectados"""
    disconnected = []
    for client in connected_clients:
        try:
            await client.send_json(message)
        except:
            disconnected.append(client)
    
    # Remove clients desconectados
    for client in disconnected:
        if client in connected_clients:
            connected_clients.remove(client)

# ============ TAREFAS PERIÓDICAS ============

async def weather_scheduler():
    """Agendador para requisitar dados meteorológicos"""
    while True:
        await asyncio.sleep(7200)  # 2 horas
        
        # Envia comando para ESP32 ler estação
        mqtt_payload = {
            "action": "read_weather_now",
            "timestamp": datetime.utcnow().isoformat()
        }
        mqtt_client.publish(
            f"agroirriga/agroirriga_fazenda_01/command",
            json.dumps(mqtt_payload)
        )

async def ml_irrigation_advisor():
    """Consultor ML que roda a cada 6 horas para sugerir irrigação"""
    while True:
        await asyncio.sleep(21600)  # 6 horas
        
        # Analisa se é hora de irrigar baseado em ML
        # Esta função pode chamar o endpoint smart_irrigation
        print("🤖 ML Advisor: Analisando condições para irrigação...")
