"""Mock data generator for sleep monitoring demo"""
import asyncio
import random
import sys
import time
from pathlib import Path
from datetime import datetime

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))

from backend.pipeline import RawSample, SleepPipeline
from backend import database

class MockDataGenerator:
    """Generates realistic sleep monitoring data with various scenarios"""
    
    def __init__(self):
        self.start_time = int(time.time() * 1000)
        self.current_time = self.start_time
        self.scenario = "calm"  # calm, restless, movement, awake
        self.scenario_duration = 0
        
    def generate_sample(self) -> RawSample:
        """Generate one sample with realistic sensor readings"""
        
        # Change scenarios periodically
        if self.scenario_duration <= 0:
            self.scenario = random.choice([
                "calm", "calm", "calm",  # mostly calm sleep
                "slight", "slight",      # some movement
                "restless",              # restless period
                "movement"               # active movement
            ])
            self.scenario_duration = random.randint(30, 120)  # 3-12 seconds
        
        self.scenario_duration -= 1
        
        # Generate piezo sensor values based on scenario
        if self.scenario == "calm":
            # Very quiet sleep - minimal sensor readings
            head_base = random.randint(490, 510)
            body_base = random.randint(490, 510)
            leg_base = random.randint(490, 510)
            # Occasional tiny movements
            if random.random() < 0.05:  # 5% chance
                head_base += random.randint(5, 15)
            if random.random() < 0.03:
                body_base += random.randint(5, 20)
                
        elif self.scenario == "slight":
            # Slight movements - minor position adjustments
            head_base = random.randint(495, 520)
            body_base = random.randint(495, 525)
            leg_base = random.randint(495, 530)
            # More frequent small movements
            if random.random() < 0.15:
                head_base += random.randint(10, 30)
            if random.random() < 0.20:
                body_base += random.randint(15, 40)
            if random.random() < 0.15:
                leg_base += random.randint(10, 35)
                
        elif self.scenario == "restless":
            # Restless period - continuous low-level movement
            head_base = random.randint(500, 530)
            body_base = random.randint(505, 540)
            leg_base = random.randint(500, 545)
            # Frequent moderate movements
            if random.random() < 0.30:
                head_base += random.randint(20, 50)
            if random.random() < 0.35:
                body_base += random.randint(25, 60)
            if random.random() < 0.25:
                leg_base += random.randint(20, 55)
                
        else:  # movement
            # Active movement - turning, repositioning
            head_base = random.randint(510, 560)
            body_base = random.randint(520, 580)
            leg_base = random.randint(515, 570)
            # High intensity spikes
            if random.random() < 0.40:
                head_base += random.randint(30, 80)
            if random.random() < 0.50:
                body_base += random.randint(40, 100)
            if random.random() < 0.35:
                leg_base += random.randint(30, 90)
        
        # Sound sensor (ambient noise)
        sound_base = random.randint(80, 120)
        if random.random() < 0.05:  # occasional noise
            sound_base += random.randint(50, 150)
        
        # Light sensor (should be dark during sleep)
        light = random.randint(50, 150)  # low light
        if random.random() < 0.02:  # rare light changes
            light = random.randint(300, 800)
        
        # Temperature sensor (room temp ~20-24°C)
        # ADC reading for temp: (temp_c / 100 + 0.5) * 1023 / 5
        temp_c = random.uniform(20.0, 24.0)
        temp_adc = int((temp_c / 100.0 + 0.5) * 1023.0 / 5.0)
        
        # Button (not pressed)
        button = 1
        
        sample = RawSample(
            time_ms=self.current_time - self.start_time,
            piezo={
                "head": head_base,
                "body": body_base,
                "leg": leg_base,
            },
            sound=sound_base,
            light=light,
            temp_raw=temp_adc,
            button=button,
        )
        
        self.current_time += 100  # 100ms per sample (10Hz)
        return sample


async def generate_mock_data(duration_seconds: int = 300):
    """Generate mock data for specified duration"""
    print(f"🎭 Starting mock data generation for {duration_seconds} seconds...")
    print(f"📊 This will create realistic sleep monitoring data with varying scenarios")
    
    generator = MockDataGenerator()
    pipeline = SleepPipeline()
    
    # Clear existing data
    print("🗑️  Clearing existing data...")
    await asyncio.to_thread(database.clear_samples)
    
    samples_generated = 0
    start_real_time = time.time()
    
    print("✨ Generating data...")
    
    # Generate samples at 10Hz for the specified duration
    num_samples = duration_seconds * 10
    
    for i in range(num_samples):
        sample = generator.generate_sample()
        events = pipeline.process_sample(sample)
        
        # Save processed data to database
        for event in events:
            if event.get("type") == "data":
                processed = event["payload"]
                await asyncio.to_thread(database.save_processed_sample, processed)
                samples_generated += 1
                
                # Print progress every 10 seconds of simulated time
                if samples_generated % 10 == 0:
                    print(f"  ⏱️  Generated {int(processed.time_s)}s of data | "
                          f"Sleep Score: {processed.sleep_score:.1f} | "
                          f"Status: {processed.status_label}")
        
        # Small delay to not overwhelm the system
        if i % 100 == 0:
            await asyncio.sleep(0.01)
    
    elapsed = time.time() - start_real_time
    print(f"\n✅ Mock data generation complete!")
    print(f"📈 Generated {samples_generated} processed samples ({duration_seconds}s of sleep data)")
    print(f"⚡ Completed in {elapsed:.2f} seconds")
    print(f"\n🌐 Open http://127.0.0.1:8000/ to view the dashboard with demo data!")


if __name__ == "__main__":
    # Generate 5 minutes of data by default
    asyncio.run(generate_mock_data(duration_seconds=300))

