@file:OptIn(ExperimentalFoundationApi::class)

package com.norsehorse.scrubpony.ui.onboarding

import androidx.compose.foundation.ExperimentalFoundationApi

import androidx.compose.animation.core.animateDpAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.norsehorse.scrubpony.R
import com.norsehorse.scrubpony.ScrubPonyTheme
import kotlinx.coroutines.launch

private data class OnboardPage(
    val glyph: String,
    val titleRes: Int,
    val bodyRes: Int,
    val accentTitle: Boolean = false,
)

private val PAGES = listOf(
    OnboardPage("🐴", R.string.onboard_welcome_title, R.string.onboard_welcome_body, accentTitle = true),
    OnboardPage("🔒", R.string.onboard_local_title, R.string.onboard_local_body),
    OnboardPage("🖼", R.string.onboard_formats_title, R.string.onboard_formats_body),
    OnboardPage("📤", R.string.onboard_share_title, R.string.onboard_share_body),
)

/**
 * First-run tour. Four pages the user swipes through, or skips. The last page
 * swaps the "Next" button for a "Get started" that calls [onFinish], which is
 * where MainActivity records that the tour has been seen. Also reachable later
 * from Settings ("Replay the tour"), so it must not assume it is truly the
 * first thing the user ever sees.
 */
@Composable
fun OnboardingScreen(onFinish: () -> Unit) {
    val pagerState = rememberPagerState(pageCount = { PAGES.size })
    val scope = rememberCoroutineScope()
    val onLastPage = pagerState.currentPage == PAGES.lastIndex

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(ScrubPonyTheme.background)
            .padding(24.dp),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.End,
        ) {
            if (!onLastPage) {
                TextButton(onClick = onFinish) {
                    Text(stringResource(R.string.onboard_skip), color = ScrubPonyTheme.dim)
                }
            } else {
                Spacer(Modifier.height(48.dp))
            }
        }

        HorizontalPager(
            state = pagerState,
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth(),
        ) { page ->
            PageContent(PAGES[page])
        }

        PageIndicator(count = PAGES.size, current = pagerState.currentPage)

        Spacer(Modifier.height(24.dp))

        Button(
            onClick = {
                if (onLastPage) {
                    onFinish()
                } else {
                    scope.launch { pagerState.animateScrollToPage(pagerState.currentPage + 1) }
                }
            },
            modifier = Modifier.fillMaxWidth().height(52.dp),
            shape = RoundedCornerShape(14.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = ScrubPonyTheme.accent,
                contentColor = ScrubPonyTheme.onAccent,
            ),
        ) {
            Text(
                stringResource(if (onLastPage) R.string.onboard_start else R.string.onboard_next),
                style = MaterialTheme.typography.titleMedium,
            )
        }
    }
}

@Composable
private fun PageContent(page: OnboardPage) {
    Column(
        modifier = Modifier.fillMaxSize(),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Box(
            modifier = Modifier
                .size(112.dp)
                .background(ScrubPonyTheme.panel, CircleShape),
            contentAlignment = Alignment.Center,
        ) {
            Text(page.glyph, style = MaterialTheme.typography.displaySmall)
        }
        Spacer(Modifier.height(36.dp))
        Text(
            stringResource(page.titleRes),
            style = MaterialTheme.typography.headlineMedium,
            color = if (page.accentTitle) ScrubPonyTheme.accentBright else ScrubPonyTheme.ink,
            textAlign = TextAlign.Center,
        )
        Spacer(Modifier.height(14.dp))
        Text(
            stringResource(page.bodyRes),
            style = MaterialTheme.typography.bodyLarge,
            color = ScrubPonyTheme.dim,
            textAlign = TextAlign.Center,
            modifier = Modifier.padding(horizontal = 8.dp),
        )
    }
}

@Composable
private fun PageIndicator(count: Int, current: Int) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        repeat(count) { i ->
            val active = i == current
            val width by animateDpAsState(if (active) 22.dp else 8.dp, label = "dotWidth")
            Box(
                modifier = Modifier
                    .padding(horizontal = 4.dp)
                    .size(width = width, height = 8.dp)
                    .background(
                        if (active) ScrubPonyTheme.accent else ScrubPonyTheme.line,
                        CircleShape,
                    ),
            )
        }
    }
}
