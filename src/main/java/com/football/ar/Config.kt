package com.football.ar

/**
 * Backend configuration — change this when deploying to a different server.
 */
object Config {
    const val BACKEND_HOST = "102.220.31.70"
    const val BACKEND_PORT = 8080

    val catalogUrl: String get() = "http://$BACKEND_HOST:8005"
    val apiUrl: String get() = "http://$BACKEND_HOST:$BACKEND_PORT"
    val sessionUrl: String get() = "http://$BACKEND_HOST:8002"
}
