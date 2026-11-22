FROM python:3.12-slim
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY app.py .
COPY static ./static
ENV PYTHONUNBUFFERED=1
USER nobody
EXPOSE 8095
CMD ["python", "app.py"]
