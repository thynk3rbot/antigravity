import sqlalchemy as sa
from sqlalchemy.orm import sessionmaker
from sqlalchemy.sql import text
from monitor import monitor
import os
import time

class MagicPersistence:
  def __init__(self, db_url=None):
    self.db_url = db_url or os.getenv("MAGIC_CACHE_DB_URL", "postgresql://magic:magic@localhost:5432/magic_cache")
    self._engine = None
    self._Session = None
    self.metadata = sa.MetaData()

  @property
  def engine(self):
    """Lazy-loading engine with connection retries."""
    if self._engine is None:
      self._ensure_connection()
    return self._engine

  def _ensure_connection(self, retries=5, delay=2):
    """Robust connection attempts for cold-boot environments."""
    for i in range(retries):
      try:
        self._engine = sa.create_engine(self.db_url)
        # Test connection
        with self._engine.connect() as conn:
          conn.execute(text("SELECT 1"))
        self._Session = sessionmaker(bind=self._engine)
        monitor.log_info("Persistence", f"Connected to Magic DB (Attempt {i+1})")
        return
      except Exception as e:
        monitor.log_error("Persistence", f"DB Connection Attempt {i+1} failed: {e}")
        if i < retries - 1:
          time.sleep(delay)
        else:
          raise

  def get_table_name(self, subject):
    """Map dots to underscores for SQL compliance."""
    return subject.replace(".", "_").replace("/", "_")

  def sync_record(self, subject, kv_pairs):
    """Merge KV pairs into RDBMS. Auto-evolves schema."""
    table_name = self.get_table_name(subject)
    
    with self.engine.connect() as conn:
      # 1. Ensure table exists
      inspector = sa.inspect(self.engine)
      if not inspector.has_table(table_name):
        self._create_magic_table(conn, table_name, kv_pairs)
      else:
        self._evolve_magic_table(conn, table_name, kv_pairs)

      # 2. Perform Upsert (Update-is-Replace)
      columns = ", ".join(kv_pairs.keys())
      placeholders = ", ".join([f":{k}" for k in kv_pairs.keys()])
      kv_pairs['magic_subject'] = subject
      
      try:
        conn.execute(text(f"DELETE FROM {table_name} WHERE magic_subject = :magic_subject"), {"magic_subject": subject})
        conn.execute(text(f"INSERT INTO {table_name} (magic_subject, {columns}) VALUES (:magic_subject, {placeholders})"), kv_pairs)
        conn.commit()
      except Exception as e:
        monitor.log_error("Persistence", f"Sync failed for {subject}: {e}")

  def _create_magic_table(self, conn, table_name, kv_pairs):
    """Create table with initial columns based on KV pairs."""
    cols = ["magic_subject VARCHAR(255) PRIMARY KEY"]
    for key, value in kv_pairs.items():
      if key == 'magic_subject':
        continue
      col_type = self._guess_type(value)
      cols.append(f"{key} {col_type}")
    
    sql = f"CREATE TABLE {table_name} ({', '.join(cols)})"
    conn.execute(text(sql))
    conn.commit()
    monitor.log_info("Persistence", f"Created NEW 'Magic' Table: {table_name}")

  def _evolve_magic_table(self, conn, table_name, kv_pairs):
    """Check for missing columns and ALTER TABLE if needed."""
    inspector = sa.inspect(self.engine)
    existing_cols = [c['name'] for c in inspector.get_columns(table_name)]
    
    for key, value in kv_pairs.items():
      if key not in existing_cols:
        col_type = self._guess_type(value)
        sql = f"ALTER TABLE {table_name} ADD COLUMN {key} {col_type}"
        try:
          conn.execute(text(sql))
          conn.commit()
          monitor.log_info("Persistence", f"Evolved Schema: Added {key} to {table_name}")
        except Exception as e:
          monitor.log_error("Persistence", f"Schema evolution failed for {key}: {e}")

  def _guess_type(self, value):
    """Simplistic type mapping for dynamic columns."""
    if isinstance(value, bool):
      return "BOOLEAN"
    if isinstance(value, int):
      return "INTEGER"
    if isinstance(value, float):
      return "NUMERIC"
    return "TEXT"

# Global persistence instance
persistence = MagicPersistence()
