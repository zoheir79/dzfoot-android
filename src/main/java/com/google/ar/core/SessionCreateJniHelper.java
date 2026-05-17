package com.google.ar.core;

import android.content.Context;

/**
 * Helper class used by ARCore's native session creation.
 * Do not remove or change the name of this class or its methods.
 */
public class SessionCreateJniHelper {
  public static ClassLoader getClassLoader(Context context) {
    return context.getClassLoader();
  }
}
