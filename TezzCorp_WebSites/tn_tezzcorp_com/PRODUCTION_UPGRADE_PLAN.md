# TezzCorp Production Upgrade Master Plan

## Objective
Build TezzCorp into a production-grade software company platform with:
- A conversion-focused public website
- A full CRM and project operations system
- Website delivery and hosting lifecycle management
- API product, subscription, and usage management
- Strong security, observability, and release governance

## Phase 1: Foundation and Frontend (Now)
- Upgrade homepage experience for enterprise buyers
- Standardize header/footer and primary navigation
- Harden shared frontend scripts for null-safe behavior
- Add production database foundation for website and API management
- Fix critical CRM API blockers that prevent normal operation

## Phase 2: CRM Completion
- Implement Deals, Tickets, Invoices, Payments, Security modules
- Add role-based dashboards (Founder, Sales, PM, Support, Finance)
- Add activity timeline, reminders, SLA tracking, and pipeline automation
- Build global search across accounts, contacts, leads, projects, invoices

## Phase 3: Website Operations Platform
- Manage client websites as assets with environments (dev/stage/prod)
- Domain and SSL lifecycle tracking
- Deployment history with rollback visibility
- Website page/CMS revisions and redirect management

## Phase 4: API Product Platform
- API product catalog and pricing plans
- Client subscriptions and billing states
- Per-client usage metering, error rates, and p95 latency tracking
- Webhook management with delivery logs and retry trails

## Phase 5: Security and Compliance
- Enforce least-privilege RBAC and permission audits
- 2FA and session controls for privileged users
- Security event analytics and suspicious pattern alerts
- Data retention and backup/restore drills

## Phase 6: Reliability and Delivery
- Staging and production environments with release checklist gates
- Migrations as versioned scripts + rollback plans
- Structured logs, uptime probes, and alert routing
- Weekly release cycle with regression and smoke test packs

## Data and Architecture Principles
- Organization-scoped data isolation in all major entities
- UUID32 keys for consistency with current schema
- Soft deletes for business entities, immutable logs for audits
- Idempotent writes for callbacks/webhooks/payments
- Explicit status enums for business workflow state machines

## KPI Targets
- Website lead conversion rate uplift (baseline + 25% target)
- CRM pipeline response time under 24h for all inbound leads
- Deployment success rate above 98%
- API subscription churn under 3% monthly
- P95 API latency under 300ms for core endpoints

## Immediate Deliverables in This Update
- `database/2026-02-09-production-foundation.sql`
- Production homepage and shared frontend improvements
- Critical CRM API fixes needed for production readiness
