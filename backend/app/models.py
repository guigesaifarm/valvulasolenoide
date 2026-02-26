"""
Modelos SQLAlchemy para PostgreSQL
"""

from sqlalchemy import Column, Integer, String, Float, Boolean, DateTime, ForeignKey, JSON
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import relationship
from datetime import datetime

Base = declarative_base()

class SolenoidState(Base):
    __tablename__ = "solenoid_states"
    
    id = Column(Integer, primary_key=True, index=True)
    valve_number = Column(Integer, nullable=False)  # 1-10
    state = Column(Boolean, default=False)  # True=ON, False=OFF
    timestamp = Column(DateTime, default=datetime.utcnow)
    device_id = Column(String(50), default="agroirriga_fazenda_01")
    
    # Índice para buscas rápidas
    __table_args__ = (
        Index('idx_valve_time', 'valve_number', 'timestamp'),
    )

class WeatherReading(Base):
    __tablename__ = "weather_readings"
    
    id = Column(Integer, primary_key=True, index=True)
    temperature = Column(Float)  # Celsius
    humidity = Column(Float)     # %
    pressure = Column(Float)     # hPa
    wind_speed = Column(Float)   # km/h
    wind_direction = Column(Float)  # graus
    rain_1h = Column(Float)        # mm
    rain_today = Column(Float)   # mm
    solar_radiation = Column(Float)  # W/m²
    uv_index = Column(Float)
    timestamp = Column(DateTime, default=datetime.utcnow)
    device_id = Column(String(50))

class IrrigationEvent(Base):
    __tablename__ = "irrigation_events"
    
    id = Column(Integer, primary_key=True, index=True)
    valve_number = Column(Integer, ForeignKey("solenoid_states.valve_number"))
    zone_id = Column(Integer, default=1)
    action = Column(String(20))  # ON, OFF, SMART_ON
    duration_minutes = Column(Integer, nullable=True)
    triggered_by = Column(String(50))  # manual, schedule, ml_model, weather_rule
    confidence = Column(Float, nullable=True)  # Para decisões ML
    water_used_liters = Column(Float, nullable=True)  # Calculado via fluxo
    timestamp = Column(DateTime, default=datetime.utcnow)
    
    # Dados contextuais
    weather_temp = Column(Float)
    weather_humidity = Column(Float)
    soil_moisture = Column(Float)

class IrrigationSchedule(Base):
    __tablename__ = "irrigation_schedules"
    
    id = Column(Integer, primary_key=True, index=True)
    valve_number = Column(Integer, nullable=False)
    zone_id = Column(Integer, default=1)
    start_time = Column(DateTime)  # Hora do dia
    duration_minutes = Column(Integer, default=30)
    days_of_week = Column(JSON)  # [1,3,5] = seg, qua, sex
    active = Column(Boolean, default=True)
    weather_dependent = Column(Boolean, default=True)  # Pula se choveu
    created_at = Column(DateTime, default=datetime.utcnow)

class FarmZone(Base):
    __tablename__ = "farm_zones"
    
    id = Column(Integer, primary_key=True, index=True)
    name = Column(String(100))
    description = Column(String(255))
    valves = Column(JSON)  # [1,2,3] - quais válvulas pertencem à zona
    crop_type = Column(String(50), default="soy")  # soja, milho, etc
    area_hectares = Column(Float)
    coordinates = Column(JSON)  # GeoJSON do polígono
    created_at = Column(DateTime, default=datetime.utcnow)
