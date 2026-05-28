'use strict';

const { DynamoDBClient } = require('@aws-sdk/client-dynamodb');
const { DynamoDBDocumentClient } = require('@aws-sdk/lib-dynamodb');

const client = new DynamoDBClient({});
const db = DynamoDBDocumentClient.from(client, {
  marshallOptions: { removeUndefinedValues: true },
});

const TABLES = {
  DEVICES:   process.env.DEVICES_TABLE   || 'gecko_devices',
  TELEMETRY: process.env.TELEMETRY_TABLE || 'gecko_telemetry',
  ENERGY:    process.env.ENERGY_TABLE    || 'gecko_energy',
};

// 90-day TTL for telemetry records
const telemetryTtl = () => Math.floor(Date.now() / 1000) + 90 * 24 * 3600;

const response = (statusCode, body, extraHeaders = {}) => ({
  statusCode,
  headers: { 'Content-Type': 'application/json', ...extraHeaders },
  body: JSON.stringify(body),
});

const ok  = (body)          => response(200, body);
const created = (body)      => response(201, body);
const badRequest = (msg)    => response(400, { error: msg });
const notFound = (msg)      => response(404, { error: msg || 'Not found' });
const serverError = (msg)   => response(500, { error: msg || 'Internal server error' });

module.exports = { db, TABLES, telemetryTtl, ok, created, badRequest, notFound, serverError };
