import { betterAuth } from 'better-auth'
import { authConfig } from './auth-config.js'

export const auth = betterAuth(authConfig)
